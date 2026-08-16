#pragma once

#include <cstddef>

#include "messages.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "types.hpp"

template <std::size_t MaxOrders, std::size_t PriceLevels, std::size_t InboundCapacity, std::size_t OutboundCapacity>
class MatchingEngine
{
public:
    MatchingEngine(Price min_price, Price tick_size,
                   SPSCQueue<InboundMessage, InboundCapacity> &inbound,
                   SPSCQueue<OutboundEvent, OutboundCapacity> &outbound)
        : book_(min_price, tick_size), inbound_(inbound), outbound_(outbound) {}

    MatchingEngine(const MatchingEngine &) = delete;
    MatchingEngine &operator=(const MatchingEngine &) = delete;

    std::size_t poll() noexcept
    {
        std::size_t processed = 0;
        InboundMessage msg;
        while (inbound_.try_pop(msg))
        {
            dispatch(msg);
            ++processed;
        }
        return processed;
    }

    [[nodiscard]] OrderBook<MaxOrders, PriceLevels> &book() { return book_; }
    [[nodiscard]] const OrderBook<MaxOrders, PriceLevels> &book() const { return book_; }

private:
    void dispatch(const InboundMessage &msg) noexcept
    {
        auto sink = [this](const OutboundEvent &ev) noexcept
        {
            (void)outbound_.try_push(ev);
        };

        switch (msg.type)
        {
        case MessageType::NEW_ORDER:
            book_.submit_order(msg.order_id, msg.side, msg.order_type, msg.price, msg.quantity, msg.timestamp, sink);
            break;
        case MessageType::CANCEL_ORDER:
            book_.cancel_order(msg.order_id, msg.timestamp, sink);
            break;
        }
    }

    OrderBook<MaxOrders, PriceLevels> book_;
    SPSCQueue<InboundMessage, InboundCapacity> &inbound_;
    SPSCQueue<OutboundEvent, OutboundCapacity> &outbound_;
};
