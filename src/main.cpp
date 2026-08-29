#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#include "util.hpp"
#include "matching_engine.hpp"

namespace
{
    constexpr std::size_t MAX_ORDERS = 8192;
    constexpr std::size_t PRICE_LEVELS = 1024;
    constexpr std::size_t INBOUND_CAPACITY = 4096;
    constexpr std::size_t OUTBOUND_CAPACITY = 16384;

    constexpr Price TICK_SIZE = 1;
    constexpr Price MIN_PRICE = 9000;
    constexpr Price MID_PRICE = MIN_PRICE + PRICE_LEVELS / 2;
    constexpr int SPREAD = 50;
    constexpr Quantity MAX_ORDER_QTY = 20;
    constexpr std::size_t CANCEL_RING_SIZE = 64;
    constexpr std::size_t DEFAULT_NUM_ORDERS = 500'000;
    constexpr std::size_t VERBOSE_EVENT_LIMIT = 20;

    using Inbound = SPSCQueue<InboundMessage, INBOUND_CAPACITY>;
    using Outbound = SPSCQueue<OutboundEvent, OUTBOUND_CAPACITY>;
    using Engine = MatchingEngine<MAX_ORDERS, PRICE_LEVELS, INBOUND_CAPACITY, OUTBOUND_CAPACITY>;

    struct Stats
    {
        std::uint64_t acked = 0;
        std::uint64_t trades = 0;
        std::uint64_t traded_quantity = 0;
        std::uint64_t cancelled = 0;
        std::uint64_t rejected = 0;

        void record(const OutboundEvent &ev)
        {
            switch (ev.type)
            {
            case OutboundEventType::ORDER_ACKED:
                ++acked;
                break;
            case OutboundEventType::TRADE_EXECUTED:
                ++trades;
                traded_quantity += ev.quantity;
                break;
            case OutboundEventType::ORDER_CANCELLED:
                ++cancelled;
                break;
            case OutboundEventType::ORDER_REJECTED:
                ++rejected;
                break;
            }
        }
    };

    void run_producer(Inbound &inbound, std::atomic<bool> &done, std::size_t num_orders)
    {
        constexpr int seed = 42; // the answer to the ultimate question of life, the universe, and everything.
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<int> type_dist(0, 9);
        std::uniform_int_distribution<int> offset_dist(-SPREAD, SPREAD);
        std::uniform_int_distribution<Quantity> qty_dist(1, MAX_ORDER_QTY);
        std::uniform_int_distribution<int> cancel_dist(0, 9);

        std::array<OrderId, CANCEL_RING_SIZE> recent_ids{};
        std::size_t recent_count = 0;
        std::size_t recent_cursor = 0;

        auto push = [&](const InboundMessage &msg)
        {
            while (!inbound.try_push(msg))
            {
                std::this_thread::yield();
            }
        };

        OrderId next_id = 1;
        for (std::size_t i = 0; i < num_orders; ++i)
        {
            const Timestamp ts = static_cast<Timestamp>(i);

            if (recent_count > 0 && cancel_dist(rng) == 0)
            {
                std::uniform_int_distribution<std::size_t> pick(0, recent_count - 1);
                InboundMessage cancel{};
                cancel.type = MessageType::CANCEL_ORDER;
                cancel.order_id = recent_ids[pick(rng)];
                cancel.timestamp = ts;
                push(cancel);
                continue;
            }

            InboundMessage order{};
            order.type = MessageType::NEW_ORDER;
            order.order_id = next_id++;
            order.side = (side_dist(rng) == 0) ? Side::BUY : Side::SELL;
            order.order_type = (type_dist(rng) == 0) ? OrderType::MARKET : OrderType::LIMIT;
            order.price = static_cast<Price>(static_cast<long long>(MID_PRICE) + offset_dist(rng));
            order.quantity = qty_dist(rng);
            order.timestamp = ts;
            push(order);

            recent_ids[recent_cursor] = order.order_id;
            recent_cursor = (recent_cursor + 1) & (CANCEL_RING_SIZE - 1);
            recent_count = std::min(recent_count + 1, CANCEL_RING_SIZE);
        }

        done.store(true, std::memory_order_release);
    }

    void run_benchmark(std::size_t num_orders)
    {
        static Inbound inbound;
        static Outbound outbound;
        static Engine engine(MIN_PRICE, TICK_SIZE, inbound, outbound);

        constexpr int seed = 42;
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<int> type_dist(0, 9);
        std::uniform_int_distribution<int> offset_dist(-SPREAD, SPREAD);
        std::uniform_int_distribution<Quantity> qty_dist(1, MAX_ORDER_QTY);
        std::uniform_int_distribution<int> cancel_dist(0, 9);

        std::array<OrderId, CANCEL_RING_SIZE> recent_ids{};
        std::size_t recent_count = 0;
        std::size_t recent_cursor = 0;

        // Discard the first slice from the stats so cache warm-up and
        // branch prediction settle before anything is recorded.
        const std::size_t warmup = std::min<std::size_t>(num_orders / 10, 50'000);
        std::vector<double> latencies_us;
        latencies_us.reserve(num_orders > warmup ? num_orders - warmup : 0);

        OrderId next_id = 1;
        OutboundEvent ev;

        for (std::size_t i = 0; i < num_orders; ++i)
        {
            const Timestamp ts = static_cast<Timestamp>(i);
            InboundMessage msg{};

            if (recent_count > 0 && cancel_dist(rng) == 0)
            {
                std::uniform_int_distribution<std::size_t> pick(0, recent_count - 1);
                msg.type = MessageType::CANCEL_ORDER;
                msg.order_id = recent_ids[pick(rng)];
                msg.timestamp = ts;
            }
            else
            {
                msg.type = MessageType::NEW_ORDER;
                msg.order_id = next_id++;
                msg.side = (side_dist(rng) == 0) ? Side::BUY : Side::SELL;
                msg.order_type = (type_dist(rng) == 0) ? OrderType::MARKET : OrderType::LIMIT;
                msg.price = static_cast<Price>(static_cast<long long>(MID_PRICE) + offset_dist(rng));
                msg.quantity = qty_dist(rng);
                msg.timestamp = ts;

                recent_ids[recent_cursor] = msg.order_id;
                recent_cursor = (recent_cursor + 1) & (CANCEL_RING_SIZE - 1);
                recent_count = std::min(recent_count + 1, CANCEL_RING_SIZE);
            }

            const bool timed = i >= warmup;
            const auto t0 = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

            while (!inbound.try_push(msg))
            {
                std::this_thread::yield();
            }
            engine.poll();
            while (outbound.try_pop(ev))
            {
                // drain so the next iteration starts from an empty queue
            }

            if (timed)
            {
                const auto t1 = std::chrono::steady_clock::now();
                latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
        }

        std::sort(latencies_us.begin(), latencies_us.end());
        const std::size_t n = latencies_us.size();

        auto percentile = [&](double p)
        {
            if (n == 0)
            {
                return 0.0;
            }
            const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(n - 1));
            return latencies_us[idx];
        };

        double sum = 0.0;
        for (double v : latencies_us)
        {
            sum += v;
        }
        const double mean = n > 0 ? sum / static_cast<double>(n) : 0.0;

        std::printf("\n--- latency benchmark (single-threaded, %llu warmup + %llu measured messages) ---\n",
                    static_cast<unsigned long long>(warmup), static_cast<unsigned long long>(n));
        std::printf("min    = %.3f us\n", n > 0 ? latencies_us.front() : 0.0);
        std::printf("mean   = %.3f us\n", mean);
        std::printf("p50    = %.3f us\n", percentile(0.50));
        std::printf("p90    = %.3f us\n", percentile(0.90));
        std::printf("p99    = %.3f us\n", percentile(0.99));
        std::printf("p99.9  = %.3f us\n", percentile(0.999));
        std::printf("max    = %.3f us\n", n > 0 ? latencies_us.back() : 0.0);
        std::printf("throughput = %.0f msgs/sec (measured region, single-threaded)\n",
                     mean > 0 ? 1'000'000.0 / mean : 0.0);
    }
} // namespace

int main(int argc, char **argv)
{
    std::size_t num_orders = DEFAULT_NUM_ORDERS;
    bool bench_mode = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--bench")
        {
            bench_mode = true;
            continue;
        }
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(argv[i], &end, 10);
        if (end != argv[i] && parsed > 0)
        {
            num_orders = static_cast<std::size_t>(parsed);
        }
    }

    if (bench_mode)
    {
        std::printf("running single-threaded latency benchmark with %llu synthetic orders...\n",
                    static_cast<unsigned long long>(num_orders));
        run_benchmark(num_orders);
        return 0;
    }

    static Inbound inbound;
    static Outbound outbound;
    static Engine engine(MIN_PRICE, TICK_SIZE, inbound, outbound);

    std::printf("replaying %llu synthetic orders through the matching engine...\n\n",
                static_cast<unsigned long long>(num_orders));

    std::atomic<bool> producer_done{false};
    const auto start = std::chrono::steady_clock::now();
    std::thread producer(run_producer, std::ref(inbound), std::ref(producer_done), num_orders);

    Stats stats;
    std::uint64_t total_processed = 0;
    std::uint64_t printed = 0;
    bool done = false;
    while (!done)
    {
        const std::size_t processed = engine.poll();
        total_processed += processed;

        OutboundEvent ev;
        while (outbound.try_pop(ev))
        {
            stats.record(ev);
            if (printed < VERBOSE_EVENT_LIMIT)
            {
                print_event(ev);
                ++printed;
            }
        }

        if (processed == 0)
        {
            if (producer_done.load(std::memory_order_acquire))
            {
                done = true;
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }
    producer.join();

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double seconds = std::chrono::duration<double>(elapsed).count();

    std::printf("\n%llu more events not shown\n\n", printed < stats.acked + stats.trades + stats.cancelled + stats.rejected
                                                        ? static_cast<unsigned long long>(stats.acked + stats.trades + stats.cancelled + stats.rejected - printed)
                                                        : 0ULL);
    std::printf("processed %llu messages in %.3f ms (%.0f msgs/sec)\n",
                static_cast<unsigned long long>(total_processed), seconds * 1000.0,
                seconds > 0 ? static_cast<double>(total_processed) / seconds : 0.0);
    std::printf("acks=%llu trades=%llu traded_qty=%llu cancels=%llu rejects=%llu\n",
                static_cast<unsigned long long>(stats.acked),
                static_cast<unsigned long long>(stats.trades),
                static_cast<unsigned long long>(stats.traded_quantity),
                static_cast<unsigned long long>(stats.cancelled),
                static_cast<unsigned long long>(stats.rejected));

    auto &book = engine.book();
    if (book.has_bid() && book.has_ask())
    {
        std::printf("final book: bid %llu@%llu | ask %llu@%llu | open_orders=%llu\n",
                    static_cast<unsigned long long>(book.best_bid_quantity()),
                    static_cast<unsigned long long>(book.best_bid_price()),
                    static_cast<unsigned long long>(book.best_ask_quantity()),
                    static_cast<unsigned long long>(book.best_ask_price()),
                    static_cast<unsigned long long>(book.open_order_count()));
    }
    else
    {
        std::printf("final book: one-sided or empty | open_orders=%llu\n",
                    static_cast<unsigned long long>(book.open_order_count()));
    }

    return 0;
}
