#pragma once

#include <cstdint>

#include "./types.hpp"

enum class MessageType : std::uint8_t
{
    NEW_ORDER = 0,
    CANCEL_ORDER = 1,
};

struct alignas(64) InboundMessage
{
    MessageType type = MessageType::NEW_ORDER;
    OrderId order_id = 0;
    Side side = Side::BUY;
    OrderType order_type = OrderType::LIMIT;
    Price price = 0;
    Quantity quantity = 0;
    Timestamp timestamp = 0;
};
static_assert(sizeof(InboundMessage) == 64, "InboundMessage must fit exactly one cache line");
static_assert(alignof(InboundMessage) == 64, "InboundMessage must be cache-line aligned");

enum class OutboundEventType : std::uint8_t
{
    TRADE_EXECUTED = 0,
    ORDER_ACKED = 1,
    ORDER_CANCELLED = 2,
    ORDER_REJECTED = 3,
};

struct alignas(64) OutboundEvent
{
    OutboundEventType type = OutboundEventType::ORDER_ACKED;
    OrderId taker_order_id = 0;
    OrderId maker_order_id = 0;
    Price price = 0;
    Quantity quantity = 0;
    Timestamp timestamp = 0;
    Side taker_side = Side::BUY;
};
static_assert(sizeof(OutboundEvent) == 64, "OutboundEvent must fit exactly one cache line");
static_assert(alignof(OutboundEvent) == 64, "OutboundEvent must be cache-line aligned");
