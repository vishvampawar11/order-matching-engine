# Order Matching Engine

A single-symbol order book and matching engine in C++20, built around
price-time priority with fixed-capacity, allocation-free on the hot path,
and cache-conscious.

There's no dynamic memory allocation, no locks, and no STL containers on the
order-processing path. Orders live in a flat memory pool, price levels are
tracked with a bitmap for O(1) best-bid/ask lookup, and inbound/outbound
messages move through a lock-free single-producer/single-consumer queue.

## Architecture

![flowchart](assets/flowchart.png)

`MatchingEngine::poll()` drains the inbound queue, dispatches each message to
the `OrderBook`, and pushes resulting `OutboundEvent`s onto the outbound queue.

## Building

```bash
./build.sh --clean
```

## Testing

```bash
./run_tests.sh
```
