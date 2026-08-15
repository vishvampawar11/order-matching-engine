#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template <typename T, std::size_t Capacity>
class SPSCQueue
{
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCQueue() = default;

    // making the SPSCQueue strictly non-copyable and non-movable
    SPSCQueue(const SPSCQueue &) = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;
    SPSCQueue(SPSCQueue &&) = delete;
    SPSCQueue &operator=(SPSCQueue &&) = delete;

    bool try_push(const T &item)
    {
        const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next_tail = advance(current_tail);

        if (next_tail == head_cache_)
        {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_)
            {
                return false;
            }
        }

        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool try_pop(T &out)
    {
        const std::size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_cache_)
        {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (current_head == tail_cache_)
            {
                return false;
            }
        }

        out = buffer_[current_head];
        head_.store(advance(current_head), std::memory_order_release);
        return true;
    }

    static constexpr std::size_t usable_capacity() { return Capacity - 1; }

private:
    static constexpr std::size_t INDEX_MASK = Capacity - 1;

    static constexpr std::size_t advance(std::size_t index)
    {
        return (index + 1) & INDEX_MASK;
    }

    alignas(64) std::array<T, Capacity> buffer_{};

    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_ = 0;

    alignas(64) std::atomic<std::size_t> head_{0};
    std::size_t tail_cache_ = 0;
};
