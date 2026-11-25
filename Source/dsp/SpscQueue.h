#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace kmf
{
// Single-producer / single-consumer lock-free queue (audio thread -> UI thread).
// push() never blocks; if the UI is not reading (window closed) the oldest unread
// items are simply not overwritten and new ones are dropped.
template <typename T, size_t Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed);
        const size_t next = (w + 1) % Capacity;
        if (next == readIdx.load (std::memory_order_acquire))
            return false;
        items[w] = item;
        writeIdx.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const size_t r = readIdx.load (std::memory_order_relaxed);
        if (r == writeIdx.load (std::memory_order_acquire))
            return false;
        out = items[r];
        readIdx.store ((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> items {};
    std::atomic<size_t> writeIdx { 0 }, readIdx { 0 };
};
} // namespace kmf
