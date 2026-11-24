// SignalFlux — exact integer tick/time conversion.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "midi/PlaybackClock.h"

namespace signalflux::midi {

namespace {

struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 mul64(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t aLo = a & 0xFFFFFFFFull, aHi = a >> 32;
    const std::uint64_t bLo = b & 0xFFFFFFFFull, bHi = b >> 32;
    const std::uint64_t ll = aLo * bLo, lh = aLo * bHi, hl = aHi * bLo, hh = aHi * bHi;
    const std::uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFull) + (hl & 0xFFFFFFFFull);
    return U128{hh + (lh >> 32) + (hl >> 32) + (mid >> 32), (mid << 32) | (ll & 0xFFFFFFFFull)};
}

/// 128-by-64 long division, bit by bit. Returns quotient (low 64 bits) and remainder.
void div128(U128 n, std::uint64_t d, std::uint64_t& quotient, std::uint64_t& remainder) noexcept {
    std::uint64_t q = 0, r = 0;
    for (int i = 127; i >= 0; --i) {
        const std::uint64_t bit = i >= 64 ? (n.hi >> (i - 64)) & 1u : (n.lo >> i) & 1u;
        const bool overflow = (r >> 63) != 0;
        r = (r << 1) | bit;
        if (overflow || r >= d) {
            r -= d;
            if (i < 64) q |= (std::uint64_t{1} << i);
        }
    }
    quotient = q;
    remainder = r;
}

} // namespace

std::uint64_t mulDiv(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept {
    std::uint64_t q = 0, r = 0;
    div128(mul64(a, b), c, q, r);
    return q;
}

bool PlaybackClock::mulDivRemainder(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept {
    std::uint64_t q = 0, r = 0;
    div128(mul64(a, b), c, q, r);
    return r != 0;
}

} // namespace signalflux::midi
