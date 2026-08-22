#pragma once

#include <cstddef>
#include <cstdint>

#include "messages.hpp"

constexpr std::size_t next_pow2(std::size_t v)
{
    std::size_t p = 1;
    while (p < v)
    {
        p <<= 1;
    }
    return p;
}

inline const char *event_name(OutboundEventType type)
{
    switch (type)
    {
    case OutboundEventType::TRADE_EXECUTED:
        return "TRADE";
    case OutboundEventType::ORDER_ACKED:
        return "ACK";
    case OutboundEventType::ORDER_CANCELLED:
        return "CANCEL";
    case OutboundEventType::ORDER_REJECTED:
        return "REJECT";
    }
    return "UNKNOWN";
}

void print_event(const OutboundEvent &ev)
{
    std::printf("[%s] taker=%llu maker=%llu price=%llu qty=%llu ts=%llu\n",
                event_name(ev.type),
                static_cast<unsigned long long>(ev.taker_order_id),
                static_cast<unsigned long long>(ev.maker_order_id),
                static_cast<unsigned long long>(ev.price),
                static_cast<unsigned long long>(ev.quantity),
                static_cast<unsigned long long>(ev.timestamp));
}
