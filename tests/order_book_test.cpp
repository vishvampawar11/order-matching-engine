#include <gtest/gtest.h>

#include "order_book.hpp"
#include "test_helpers.hpp"

namespace
{
    using Book = OrderBook<16, 64>;

    TEST(OrderBookTest, SubmitBidLimitOrderNoCrossRestsAndAcks)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::BUY, OrderType::LIMIT, 105, 10, 1000, recorder);

        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::ORDER_ACKED);
        EXPECT_EQ(recorder.events[0].taker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].price, 105u);
        EXPECT_EQ(recorder.events[0].quantity, 10u);

        EXPECT_TRUE(book.has_bid());
        EXPECT_FALSE(book.has_ask());
        EXPECT_EQ(book.best_bid_price(), 105u);
        EXPECT_EQ(book.best_bid_quantity(), 10u);
        EXPECT_EQ(book.open_order_count(), 1u);
    }

    TEST(OrderBookTest, SubmitAskLimitOrderNoCrossRestsAndAcks)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 10, 1000, recorder);

        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::ORDER_ACKED);
        EXPECT_EQ(recorder.events[0].taker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].price, 105u);
        EXPECT_EQ(recorder.events[0].quantity, 10u);

        EXPECT_TRUE(book.has_ask());
        EXPECT_FALSE(book.has_bid());
        EXPECT_EQ(book.best_ask_price(), 105u);
        EXPECT_EQ(book.best_ask_quantity(), 10u);
        EXPECT_EQ(book.open_order_count(), 1u);
    }

    TEST(OrderBookTest, CrossingOrderFullyMatchesRestingOrder)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 5, 1000, recorder);
        recorder.events.clear();

        book.submit_order(2, Side::BUY, OrderType::LIMIT, 105, 5, 1001, recorder);

        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::TRADE_EXECUTED);
        EXPECT_EQ(recorder.events[0].taker_order_id, 2u);
        EXPECT_EQ(recorder.events[0].maker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].price, 105u);
        EXPECT_EQ(recorder.events[0].quantity, 5u);

        EXPECT_FALSE(book.has_ask());
        EXPECT_FALSE(book.has_bid());
        EXPECT_EQ(book.open_order_count(), 0u);
    }

    TEST(OrderBookTest, PartialFillTakerRestsRemainder)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 5, 1000, recorder);
        recorder.events.clear();

        book.submit_order(2, Side::BUY, OrderType::LIMIT, 105, 8, 1001, recorder);

        ASSERT_EQ(recorder.events.size(), 2u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::TRADE_EXECUTED);
        EXPECT_EQ(recorder.events[0].quantity, 5u);
        EXPECT_EQ(recorder.events[1].type, OutboundEventType::ORDER_ACKED);
        EXPECT_EQ(recorder.events[1].taker_order_id, 2u);
        EXPECT_EQ(recorder.events[1].quantity, 3u);

        EXPECT_FALSE(book.has_ask());
        EXPECT_TRUE(book.has_bid());
        EXPECT_EQ(book.best_bid_price(), 105u);
        EXPECT_EQ(book.best_bid_quantity(), 3u);
    }

    TEST(OrderBookTest, PriceTimePriorityFillsOldestOrderFirst)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 5, 1000, recorder);
        book.submit_order(2, Side::SELL, OrderType::LIMIT, 105, 5, 1001, recorder);
        recorder.events.clear();

        book.submit_order(3, Side::BUY, OrderType::LIMIT, 105, 7, 1002, recorder);

        ASSERT_EQ(recorder.events.size(), 2u);
        EXPECT_EQ(recorder.events[0].maker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].quantity, 5u);
        EXPECT_EQ(recorder.events[1].maker_order_id, 2u);
        EXPECT_EQ(recorder.events[1].quantity, 2u);

        EXPECT_EQ(book.best_ask_quantity(), 3u);
        EXPECT_EQ(book.open_order_count(), 1u);
    }

    TEST(OrderBookTest, LimitOrderStopsAtLimitPriceAndRestsRemainder)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 5, 1000, recorder);
        book.submit_order(2, Side::SELL, OrderType::LIMIT, 107, 5, 1001, recorder);
        recorder.events.clear();

        book.submit_order(3, Side::BUY, OrderType::LIMIT, 106, 8, 1002, recorder);

        ASSERT_EQ(recorder.events.size(), 2u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::TRADE_EXECUTED);
        EXPECT_EQ(recorder.events[0].maker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].quantity, 5u);
        EXPECT_EQ(recorder.events[1].type, OutboundEventType::ORDER_ACKED);
        EXPECT_EQ(recorder.events[1].price, 106u);
        EXPECT_EQ(recorder.events[1].quantity, 3u);

        ASSERT_TRUE(book.has_ask());
        EXPECT_EQ(book.best_ask_price(), 107u);
        EXPECT_EQ(book.best_ask_quantity(), 5u);
    }

    TEST(OrderBookTest, CancelOrderRemovesFromBookAndEmitsCancelEvent)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::BUY, OrderType::LIMIT, 105, 10, 1000, recorder);
        recorder.events.clear();

        const bool cancelled = book.cancel_order(1, 2000, recorder);

        EXPECT_TRUE(cancelled);
        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::ORDER_CANCELLED);
        EXPECT_EQ(recorder.events[0].taker_order_id, 1u);
        EXPECT_EQ(recorder.events[0].price, 105u);
        EXPECT_EQ(recorder.events[0].quantity, 10u);

        EXPECT_FALSE(book.has_bid());
        EXPECT_EQ(book.open_order_count(), 0u);
    }

    TEST(OrderBookTest, CancelPartiallyFilledOrderReflectsRemainingQuantity)
    {
        Book book(100, 1);
        EventRecorder recorder;

        book.submit_order(1, Side::SELL, OrderType::LIMIT, 105, 10, 1000, recorder);
        book.submit_order(2, Side::BUY, OrderType::LIMIT, 105, 4, 1001, recorder);
        recorder.events.clear();

        (void)book.cancel_order(1, 2000, recorder);

        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].type, OutboundEventType::ORDER_CANCELLED);
        EXPECT_EQ(recorder.events[0].quantity, 6u);
        EXPECT_FALSE(book.has_ask());
    }
}
