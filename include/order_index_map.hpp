#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "./types.hpp"

constexpr std::size_t next_pow2(std::size_t v)
{
    std::size_t p = 1;
    while (p < v)
    {
        p <<= 1;
    }
    return p;
}

template <std::size_t Capacity>
class alignas(64) OrderIndexMap
{
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    OrderIndexMap() noexcept = default;

    OrderIndexMap(const OrderIndexMap &) = delete;
    OrderIndexMap &operator=(const OrderIndexMap &) = delete;

    bool insert(OrderId order_id, PoolIndex pool_index)
    {
        std::size_t slot = home_slot(order_id);
        for (std::size_t probes = 0; probes < Capacity; ++probes)
        {
            if (!slots_[slot].occupied)
            {
                slots_[slot] = Slot{order_id, pool_index, true};
                ++size_;
                return true;
            }
            slot = next(slot);
        }
        return false;
    }

    PoolIndex find(OrderId order_id) const noexcept
    {
        std::size_t slot = home_slot(order_id);
        for (std::size_t probes = 0; probes < Capacity; ++probes)
        {
            const Slot &s = slots_[slot];
            if (!s.occupied)
            {
                return INVALID_POOL_INDEX;
            }
            if (s.id == order_id)
            {
                return s.pool_index;
            }
            slot = next(slot);
        }
        return INVALID_POOL_INDEX;
    }

    bool erase(OrderId order_id) noexcept
    {
        std::size_t hole = home_slot(order_id);
        std::size_t probes = 0;
        while (slots_[hole].occupied && slots_[hole].id != order_id)
        {
            hole = next(hole);
            if (++probes == Capacity)
            {
                return false;
            }
        }
        if (!slots_[hole].occupied)
        {
            return false;
        }
        --size_;

        std::size_t j = hole;
        for (;;)
        {
            j = next(j);
            if (!slots_[j].occupied)
            {
                break;
            }
            const std::size_t k = home_slot(slots_[j].id);

            const bool must_stay = (hole <= j) ? (k > hole && k <= j) : (k > hole || k <= j);
            if (must_stay)
            {
                continue;
            }
            slots_[hole] = slots_[j];
            hole = j;
        }
        slots_[hole].occupied = false;
        return true;
    }

    std::size_t size() const { return size_; }
    static constexpr std::size_t capacity() { return Capacity; }

private:
    // Fibonacci hashing: multiply by an odd constant close to
    // 2^64 / golden ratio, then keep the *high* bits (the low
    // bits of a multiplicative hash are the least well-mixed).
    static constexpr std::uint64_t GOLDEN_RATIO_64 = 0x9E3779B97F4A7C15ULL;
    static constexpr int SHIFT = 64 - std::countr_zero(Capacity);

    static constexpr std::size_t home_slot(OrderId id) noexcept
    {
        return static_cast<std::size_t>((id * GOLDEN_RATIO_64) >> SHIFT);
    }

    struct Slot
    {
        OrderId id = 0;
        PoolIndex pool_index = INVALID_POOL_INDEX;
        bool occupied = false;
    };

    static constexpr std::size_t next(std::size_t slot)
    {
        return (slot + 1) & (Capacity - 1);
    }

    alignas(64) std::array<Slot, Capacity> slots_{};
    std::size_t size_ = 0;
};
