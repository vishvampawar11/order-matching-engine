#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>

#include "memory_pool.hpp"
#include "order_index_map.hpp"
#include "price_level.hpp"
#include "messages.hpp"
#include "types.hpp"
#include "util.hpp"

template <std::size_t MaxOrders, std::size_t PriceLevels>
class OrderBook
{
    static_assert(MaxOrders > 0, "MaxOrders must be positive");
    static_assert(PriceLevels > 0 && PriceLevels % 64 == 0, "PriceLevels must be a positive multiple of 64 (bitmap word width)");

public:
    OrderBook(Price min_price, Price tick_size) : min_price_(min_price), tick_size_(tick_size)
    {
        assert(tick_size_ > 0 && "tick_size must be positive");
    }

    OrderBook(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &) = delete;

    template <typename EventSink>
    void submit_order(OrderId id, Side side, OrderType type, Price price, Quantity quantity, Timestamp timestamp, EventSink &&sink) noexcept
    {
        Quantity remaining = quantity;
        match_against_book(id, side, type, price, remaining, timestamp, sink);

        if (type == OrderType::LIMIT && remaining > 0)
        {
            rest_order(id, side, price, remaining, timestamp, sink);
        }
    }

    template <typename EventSink>
    bool cancel_order(OrderId id, Timestamp timestamp, EventSink &sink)
    {
        const PoolIndex pool_index = index_map_.find(id);
        if (pool_index == INVALID_POOL_INDEX) [[unlikely]]
        {
            OutboundEvent event{};
            event.type = OutboundEventType::ORDER_REJECTED;
            event.taker_order_id = id;
            event.timestamp = timestamp;
            sink(event);
            return false;
        }

        Order &order = pool_[pool_index];
        const LevelIndex level_index = order.price_level_index;
        PriceLevel &level = levels_[level_index];

        unlink(level, pool_index);
        level.total_quantity -= order.remaining_quantity();
        --level.order_count;
        if (level.empty())
        {
            clear_level_occupied(level_index, order.side);
        }

        OutboundEvent event{};
        event.type = OutboundEventType::ORDER_CANCELLED;
        event.taker_order_id = id;
        event.price = order.price;
        event.quantity = order.remaining_quantity();
        event.timestamp = timestamp;
        event.taker_side = order.side;
        sink(event);

        index_map_.erase(id);
        pool_.release(pool_index);
        return true;
    }

    [[nodiscard]] bool has_bid() const noexcept { return best_bid_ != INVALID_LEVEL_INDEX; }
    [[nodiscard]] bool has_ask() const noexcept { return best_ask_ != INVALID_LEVEL_INDEX; }

    [[nodiscard]] Price best_bid_price() const noexcept
    {
        assert(has_bid());
        return level_to_price(best_bid_);
    }

    [[nodiscard]] Price best_ask_price() const noexcept
    {
        assert(has_ask());
        return level_to_price(best_ask_);
    }

    [[nodiscard]] Quantity best_bid_quantity() const noexcept
    {
        assert(has_bid());
        return levels_[best_bid_].total_quantity;
    }

    [[nodiscard]] Quantity best_ask_quantity() const noexcept
    {
        assert(has_ask());
        return levels_[best_ask_].total_quantity;
    }

    [[nodiscard]] std::size_t open_order_count() const noexcept { return pool_.in_use_count(); }

private:
    template <typename EventSink>
    void match_against_book(OrderId taker_id, Side side, OrderType type, Price limit_price,
                            Quantity &remaining, Timestamp timestamp, EventSink &sink)
    {
        if (side == Side::BUY)
        {
            while (remaining > 0 && best_ask_ != INVALID_LEVEL_INDEX)
            {
                if (type == OrderType::LIMIT && level_to_price(best_ask_) > limit_price)
                {
                    break;
                }
                execute_at_level(taker_id, side, best_ask_, remaining, timestamp, sink);
            }
        }
        else
        {
            while (remaining > 0 && best_bid_ != INVALID_LEVEL_INDEX)
            {
                if (type == OrderType::LIMIT && level_to_price(best_bid_) < limit_price)
                {
                    break;
                }
                execute_at_level(taker_id, side, best_bid_, remaining, timestamp, sink);
            }
        }
    }

    template <typename EventSink>
    void execute_at_level(OrderId taker_id, Side taker_side, LevelIndex level_index,
                          Quantity &remaining, Timestamp timestamp, EventSink &sink) noexcept
    {
        PriceLevel &level = levels_[level_index];
        const Price trade_price = level_to_price(level_index);

        while (remaining > 0 && !level.empty())
        {
            const PoolIndex maker_pool_index = level.head;
            Order &maker = pool_[maker_pool_index];

            const Quantity fill_qty = std::min(remaining, maker.remaining_quantity());
            maker.filled_quantity += fill_qty;
            remaining -= fill_qty;
            level.total_quantity -= fill_qty;

            OutboundEvent event{};
            event.type = OutboundEventType::TRADE_EXECUTED;
            event.taker_order_id = taker_id;
            event.maker_order_id = maker.id;
            event.price = trade_price;
            event.quantity = fill_qty;
            event.timestamp = timestamp;
            event.taker_side = taker_side;
            sink(event);

            if (maker.is_filled())
            {
                unlink(level, maker_pool_index);
                --level.order_count;
                index_map_.erase(maker.id);
                pool_.release(maker_pool_index);
            }
            else
            {
                maker.status = OrderStatus::PARTIALLY_FILLED;
            }

            if (level.empty())
            {
                clear_level_occupied(level_index, opposite_side(taker_side));
            }
        }
    }

    template <typename EventSink>
    void rest_order(OrderId id, Side side, Price price,
                    Quantity remaining, Timestamp timestamp, EventSink &sink) noexcept
    {
        if (!price_in_range(price))
        {
            reject(id, side, remaining, timestamp, sink);
            return;
        }

        const PoolIndex pool_index = pool_.acquire();
        if (pool_index == INVALID_POOL_INDEX)
        {
            reject(id, side, remaining, timestamp, sink);
            return;
        }

        Order &order = pool_[pool_index];
        order.id = id;
        order.price = price;
        order.quantity = remaining;
        order.filled_quantity = 0;
        order.timestamp = timestamp;
        order.side = side;
        order.type = OrderType::LIMIT;
        order.status = OrderStatus::NEW;
        order.price_level_index = price_to_level(price);

        const LevelIndex level_index = order.price_level_index;
        PriceLevel &level = levels_[level_index];
        const bool was_empty = level.empty();

        link_at_tail(level, pool_index);
        level.total_quantity += remaining;
        ++level.order_count;

        if (was_empty)
        {
            set_bit(level_index);
            if (side == Side::BUY)
            {
                if (best_bid_ == INVALID_LEVEL_INDEX || level_index > best_bid_)
                {
                    best_bid_ = level_index;
                }
            }
            else
            {
                if (best_ask_ == INVALID_LEVEL_INDEX || level_index < best_ask_)
                {
                    best_ask_ = level_index;
                }
            }
        }

        if (!index_map_.insert(id, pool_index))
        {
            unlink(level, pool_index);
            --level.order_count;
            level.total_quantity -= remaining;
            if (level.empty())
            {
                clear_level_occupied(level_index, side);
            }
            pool_.release(pool_index);
            reject(id, side, remaining, timestamp, sink);
            return;
        }

        OutboundEvent event{};
        event.type = OutboundEventType::ORDER_ACKED;
        event.taker_order_id = id;
        event.price = price;
        event.quantity = remaining; // Quantity now resting on the book.
        event.timestamp = timestamp;
        event.taker_side = side;
        sink(event);
    }

    template <typename EventSink>
    void reject(OrderId id, Side side, Quantity quantity, Timestamp timestamp,
                EventSink &sink) noexcept
    {
        OutboundEvent event{};
        event.type = OutboundEventType::ORDER_REJECTED;
        event.taker_order_id = id;
        event.quantity = quantity;
        event.timestamp = timestamp;
        event.taker_side = side;
        sink(event);
    }

    void link_at_tail(PriceLevel &level, PoolIndex order_index)
    {
        Order &order = pool_[order_index];
        order.prev_index = level.tail;
        order.next_index = INVALID_POOL_INDEX;
        if (level.tail != INVALID_POOL_INDEX)
        {
            pool_[level.tail].next_index = order_index;
        }
        else
        {
            level.head = order_index;
        }
        level.tail = order_index;
    }

    void unlink(PriceLevel &level, PoolIndex order_index)
    {
        Order &order = pool_[order_index];
        if (order.prev_index != INVALID_POOL_INDEX)
        {
            pool_[order.prev_index].next_index = order.next_index;
        }
        else
        {
            level.head = order.next_index;
        }
        if (order.next_index != INVALID_POOL_INDEX)
        {
            pool_[order.next_index].prev_index = order.prev_index;
        }
        else
        {
            level.tail = order.prev_index;
        }
    }

    void clear_level_occupied(LevelIndex idx, Side resting_side)
    {
        clear_bit(idx);
        if (resting_side == Side::BUY)
        {
            if (idx == best_bid_)
            {
                best_bid_ = (idx == 0) ? INVALID_LEVEL_INDEX : find_prev_set(idx - 1);
            }
        }
        else
        {
            if (idx == best_ask_)
            {
                best_ask_ = (idx + 1 >= PriceLevels) ? INVALID_LEVEL_INDEX : find_next_set(idx + 1);
            }
        }
    }

    void set_bit(LevelIndex idx)
    {
        bitmap_[idx / 64] |= (std::uint64_t{1} << (idx % 64));
    }

    void clear_bit(LevelIndex idx)
    {
        bitmap_[idx / 64] &= ~(std::uint64_t{1} << (idx % 64));
    }

    LevelIndex find_prev_set(LevelIndex start) const
    {
        std::size_t word = start / 64;
        const unsigned bit = start % 64;
        std::uint64_t w = bitmap_[word] & ((bit == 63) ? ~std::uint64_t{0}
                                                       : ((std::uint64_t{1} << (bit + 1)) - 1));
        for (;;)
        {
            if (w != 0)
            {
                const unsigned highest = 63 - static_cast<unsigned>(std::countl_zero(w));
                return static_cast<LevelIndex>(word * 64 + highest);
            }
            if (word == 0)
            {
                return INVALID_LEVEL_INDEX;
            }
            --word;
            w = bitmap_[word];
        }
    }

    LevelIndex find_next_set(LevelIndex start) const
    {
        std::size_t word = start / 64;
        const unsigned bit = start % 64;
        std::uint64_t w = bitmap_[word] & (~std::uint64_t{0} << bit);
        for (;;)
        {
            if (w != 0)
            {
                const unsigned lowest = static_cast<unsigned>(std::countr_zero(w));
                return static_cast<LevelIndex>(word * 64 + lowest);
            }
            ++word;
            if (word == BITMAP_WORDS)
            {
                return INVALID_LEVEL_INDEX;
            }
            w = bitmap_[word];
        }
    }

    bool price_in_range(Price price) const noexcept
    {
        if (price < min_price_)
        {
            return false;
        }
        const Price offset = price - min_price_;
        if (offset % tick_size_ != 0)
        {
            return false; // Not aligned to the instrument's tick size.
        }
        return (offset / tick_size_) < PriceLevels;
    }

    LevelIndex price_to_level(Price price) const
    {
        return static_cast<LevelIndex>((price - min_price_) / tick_size_);
    }

    Price level_to_price(LevelIndex index) const
    {
        return min_price_ + static_cast<Price>(index) * tick_size_;
    }

    static constexpr std::size_t INDEX_MAP_CAPACITY = next_pow2(MaxOrders * 2);
    static constexpr std::size_t BITMAP_WORDS = PriceLevels / 64;

    MemoryPool<Order, MaxOrders> pool_;
    OrderIndexMap<INDEX_MAP_CAPACITY> index_map_;

    alignas(64) std::array<PriceLevel, PriceLevels> levels_{};
    alignas(64) std::array<std::uint64_t, BITMAP_WORDS> bitmap_{};

    LevelIndex best_bid_ = INVALID_LEVEL_INDEX;
    LevelIndex best_ask_ = INVALID_LEVEL_INDEX;

    Price min_price_;
    Price tick_size_;
};
