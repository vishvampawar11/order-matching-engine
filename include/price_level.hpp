#pragma once

#include "./types.hpp"

struct PriceLevel
{
    PoolIndex head = INVALID_POOL_INDEX;
    PoolIndex tail = INVALID_POOL_INDEX;
    Quantity total_quantity = 0;
    std::uint32_t order_count = 0;

    constexpr bool empty() const { return head == INVALID_POOL_INDEX; }
};
