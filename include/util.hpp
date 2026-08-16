#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t next_pow2(std::size_t v)
{
    std::size_t p = 1;
    while (p < v)
    {
        p <<= 1;
    }
    return p;
}
