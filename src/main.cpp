#include <cstdio>

#include "matching_engine.hpp"

const char *event_name(OutboundEventType type)
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

int main()
{
    SPSCQueue<InboundMessage, 1024> inbound;
    SPSCQueue<OutboundEvent, 1024> outbound;
    MatchingEngine<1024, 65536, 1024, 1024> engine(100, 1, inbound, outbound);

    InboundMessage new_order{};
    new_order.type = MessageType::NEW_ORDER;
    new_order.order_id = 1;
    new_order.side = Side::BUY;
    new_order.order_type = OrderType::LIMIT;
    new_order.price = 105;
    new_order.quantity = 10;
    new_order.timestamp = 1000;
    inbound.try_push(new_order);

    new_order.order_id = 2;
    new_order.side = Side::SELL;
    new_order.price = 105;
    new_order.quantity = 4;
    new_order.timestamp = 1001;
    inbound.try_push(new_order);

    InboundMessage cancel{};
    cancel.type = MessageType::CANCEL_ORDER;
    cancel.order_id = 1;
    cancel.timestamp = 1002;
    inbound.try_push(cancel);

    engine.poll();

    OutboundEvent ev;
    while (outbound.try_pop(ev))
    {
        print_event(ev);
    }

    return 0;
}
