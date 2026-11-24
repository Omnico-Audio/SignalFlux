// SignalFlux — bounded single-producer/single-consumer queue (ARCHITECTURE §7).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Lock-free and wait-free: push and pop never block and never allocate.
// Exactly one producer thread and one consumer thread. When full, push()
// returns false and the caller applies its documented overflow policy.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace signalflux::midi {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padding from alignas is intentional: keeps head and tail on separate cache lines
#endif

template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /// Producer thread only.
    bool push(T value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail == Capacity) return false;
        slots_[head & (Capacity - 1)] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer thread only.
    std::optional<T> pop() noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        if (head == tail) return std::nullopt;
        T value = std::move(slots_[tail & (Capacity - 1)]);
        tail_.store(tail + 1, std::memory_order_release);
        return value;
    }

    /// Approximate when called concurrently; exact from either thread when the other is idle.
    [[nodiscard]] std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace signalflux::midi
