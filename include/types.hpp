#pragma once

#include <cstdint>

using Price = std::uint64_t;
using Quantity = std::uint64_t;
using OrderId = std::uint64_t;
using TradeId = std::uint64_t;
using Timestamp = std::uint64_t;

enum class Side : std::uint8_t
{
    BUY = 0,
    SELL = 1
};

constexpr Side opposite_side(Side side)
{
    return (side == Side::BUY) ? Side::SELL : Side::BUY;
}

enum class OrderType : std::uint8_t
{
    LIMIT = 0,
    MARKET = 1
};

enum class OrderStatus : std::uint8_t
{
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELED = 3,
    REJECTED = 4
};

// Payload is 55 bytes -> padded to 64, one cache line.
struct alignas(64) Order
{
    OrderId id = 0;
    Price price = 0;
    Quantity quantity = 0;
    Quantity filled_quantity = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    OrderStatus status = OrderStatus::NEW;
    Timestamp timestamp = 0;

    constexpr Quantity remaining_quantity() const
    {
        return quantity - filled_quantity;
    }

    constexpr bool is_filled() const
    {
        return filled_quantity >= quantity;
    }
};
static_assert(sizeof(Order) == 64, "Order must fit exactly one cache line");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

// Payload is 49 bytes -> padded to 64, one cache line.
struct alignas(64) Trade
{
    TradeId id = 0;
    OrderId taker_order_id = 0; // incoming (aggresive) order
    OrderId maker_order_id = 0; // resting (passive) order
    Price price = 0;
    Quantity quantity = 0;
    Timestamp timestamp = 0;
    Side taker_side = Side::BUY;
};
static_assert(sizeof(Trade) == 64, "Trade must fit exactly one cache line");
static_assert(alignof(Trade) == 64, "Trade must be cache-line aligned");
