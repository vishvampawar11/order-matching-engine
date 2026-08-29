#pragma once

#include <array>
#include <cstddef>
#include <new>
#include <utility>

#include "./types.hpp"

template <typename T, std::size_t Capacity>
class MemoryPool
{
    static_assert(Capacity > 0, "Pool capacity must be positive");
    static_assert(Capacity < std::numeric_limits<PoolIndex>::max(),
                  "Pool capacity must fit in PoolIndex (uint32_t)");

public:
    MemoryPool()
    {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            free_stack_[i] = static_cast<PoolIndex>(Capacity - 1 - i);
        }
        free_count_ = Capacity;
    }

    // making the MemoryPool strictly non-copyable and non-movable
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;
    MemoryPool(MemoryPool &&) = delete;
    MemoryPool &operator=(MemoryPool &&) = delete;

    template <typename... Args>
    [[nodiscard]] PoolIndex acquire(Args &&...args) noexcept
    {
        if (free_count_ == 0) [[unlikely]]
        {
            return INVALID_POOL_INDEX;
        }
        const PoolIndex index = free_stack_[--free_count_];
        ::new (slot_address(index)) T(std::forward<Args>(args)...);
        return index;
    }

    void release(PoolIndex index) noexcept
    {
        std::launder(reinterpret_cast<T *>(slot_address(index)))->~T();
        free_stack_[free_count_++] = index;
    }

    [[nodiscard]] T &operator[](PoolIndex index) noexcept
    {
        return *std::launder(reinterpret_cast<T *>(slot_address(index)));
    }

    [[nodiscard]] const T &operator[](PoolIndex index) const noexcept
    {
        return *std::launder(reinterpret_cast<const T *>(slot_address(index)));
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] std::size_t free_count() const noexcept { return free_count_; }
    [[nodiscard]] std::size_t in_use_count() const noexcept { return Capacity - free_count_; }

private:
    struct alignas(alignof(T)) Slot
    {
        std::byte bytes[sizeof(T)];
    };

    void *slot_address(PoolIndex index) noexcept
    {
        return storage_[index].bytes;
    }

    const void *slot_address(PoolIndex index) const noexcept
    {
        return storage_[index].bytes;
    }

    alignas(64) std::array<Slot, Capacity> storage_;
    alignas(64) std::array<PoolIndex, Capacity> free_stack_;
    std::size_t free_count_ = 0;
};
