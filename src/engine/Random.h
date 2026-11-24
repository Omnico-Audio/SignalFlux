// SignalFlux — deterministic, counter-based random streams (DECISIONS D10, D25).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Every random decision draws from a stream keyed by (seed, StationId, purpose)
// with its own draw counter, so adding or editing one Station never changes
// another Station's outcomes. Values come from SplitMix64 mixing and an integer
// rejection-sampled bounded draw. <random> distributions are deliberately not
// used: their output differs between MSVC, libstdc++ and libc++.
//
// Thread ownership: owned by the Sequencer (engine thread). No allocation
// after reset().

#pragma once

#include "domain/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace signalflux::engine {

enum class RandomPurpose : std::uint8_t {
    Probability,
    Branch,
    Value,
    HumanizeVelocity,
    HumanizeTiming,
};
inline constexpr std::size_t kRandomPurposeCount = 5;

[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/// The `counter`-th raw 64-bit value of stream (seed, station, purpose).
[[nodiscard]] constexpr std::uint64_t streamValue(std::uint64_t seed, StationId station, RandomPurpose purpose,
                                                  std::uint64_t counter) noexcept {
    const std::uint64_t key = (static_cast<std::uint64_t>(station.value()) << 8) | static_cast<std::uint64_t>(purpose);
    const std::uint64_t streamSeed = splitMix64(seed ^ splitMix64(key));
    return splitMix64(streamSeed + counter * 0xD1B54A32D192ED03ull);
}

class RandomStreams {
public:
    /// Resets every counter. `stationCount` slots are indexed by the caller's
    /// Station index; ids are passed separately so streams follow identity.
    void reset(std::uint64_t seed, std::size_t stationCount) {
        seed_ = seed;
        counters_.assign(stationCount, {});
    }

    /// Keeps counters for Stations that survive an edit (remap by old index).
    void remap(const std::vector<std::ptrdiff_t>& newToOld) {
        std::vector<std::array<std::uint64_t, kRandomPurposeCount>> next(newToOld.size());
        for (std::size_t i = 0; i < newToOld.size(); ++i)
            if (newToOld[i] >= 0 && static_cast<std::size_t>(newToOld[i]) < counters_.size())
                next[i] = counters_[static_cast<std::size_t>(newToOld[i])];
        counters_ = std::move(next);
    }

    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] std::uint64_t next(std::size_t stationIndex, StationId id, RandomPurpose p) noexcept {
        auto& c = counters_[stationIndex][static_cast<std::size_t>(p)];
        return streamValue(seed_, id, p, c++);
    }

    /// Uniform integer in [0, bound). bound must be > 0. Unbiased (rejection sampling).
    [[nodiscard]] std::uint64_t below(std::size_t stationIndex, StationId id, RandomPurpose p, std::uint64_t bound) noexcept {
        const std::uint64_t threshold = (0 - bound) % bound; // 2^64 mod bound
        while (true) {
            const std::uint64_t x = next(stationIndex, id, p);
            if (x >= threshold) return x % bound;
        }
    }

    /// Uniform integer in [lo, hi], lo <= hi.
    [[nodiscard]] std::int64_t between(std::size_t stationIndex, StationId id, RandomPurpose p, std::int64_t lo,
                                       std::int64_t hi) noexcept {
        const auto span = static_cast<std::uint64_t>(hi - lo) + 1u;
        return lo + static_cast<std::int64_t>(below(stationIndex, id, p, span));
    }

private:
    std::uint64_t seed_ = 0;
    std::vector<std::array<std::uint64_t, kRandomPurposeCount>> counters_;
};

} // namespace signalflux::engine
