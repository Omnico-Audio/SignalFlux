// SignalFlux — active-note registry (ARCHITECTURE §8, DECISIONS D22, D23).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Tracks every accepted Note On with its planned Note Off. Overlapping notes
// with the same (channel, pitch) are reference-counted: every Note On is sent,
// and a Note Off is sent only when the last overlapping instance ends (D22).
// Channel and pitch are captured at Note On, so later edits to a Station can
// never strand a note.
//
// Fixed capacity, set before playback. No allocation after reset().
// Thread ownership: engine thread only.

#pragma once

#include "domain/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace signalflux::engine {

class ActiveNoteRegistry {
public:
    struct Release {
        MidiChannel channel;
        MidiNote note;
    };

    /// Clears all state and sets the cap to `capacity` simultaneous notes (D23).
    /// Reserves the maximum cap once, so later cap changes never allocate.
    /// May allocate on first use; call off the deadline-critical path (e.g. on Play).
    void reset(std::size_t capacity);

    /// Changes the cap without clearing. Notes already above a lowered cap keep
    /// sounding until their Note Off; only new notes are refused.
    void setCapacity(std::size_t capacity) noexcept { capacity_ = capacity; }

    /// Registers a note that ends at `offTick`. Returns false if the cap is
    /// reached; the caller must then not send the Note On (D23).
    [[nodiscard]] bool add(MidiChannel channel, MidiNote note, MusicalTick offTick) noexcept;

    /// Earliest pending Note Off, if any.
    [[nodiscard]] std::optional<MusicalTick> nextDue() const noexcept;

    /// Removes every instance due at or before `tick`, in (offTick, insertion)
    /// order, and calls `onRelease` for each (channel, pitch) whose count
    /// reaches zero, i.e. for each Note Off that must actually be sent.
    template <typename F>
    void releaseDue(MusicalTick tick, F&& onRelease) noexcept;

    /// Releases everything (Stop, Panic). Calls `onRelease` once per sounding
    /// (channel, pitch), ordered by channel then pitch.
    template <typename F>
    void releaseAll(F&& onRelease) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] int soundingCount(MidiChannel channel, MidiNote note) const noexcept {
        return counts_[slot(channel, note)];
    }

private:
    struct Entry {
        MidiChannel channel;
        MidiNote note;
        MusicalTick offTick;
        std::uint64_t sequence = 0;
    };

    static constexpr std::size_t slot(MidiChannel c, MidiNote n) noexcept {
        return static_cast<std::size_t>(c.value() - 1) * 128u + static_cast<std::size_t>(n.value());
    }

    std::vector<Entry> entries_;
    std::array<int, 16 * 128> counts_{};
    std::size_t capacity_ = 0;
    std::uint64_t nextSequence_ = 0;
};

template <typename F>
void ActiveNoteRegistry::releaseDue(MusicalTick tick, F&& onRelease) noexcept {
    while (true) {
        // Find the earliest due entry by (offTick, sequence). Linear scan over a
        // bounded set (<= capacity), no allocation.
        std::size_t best = entries_.size();
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            if (e.offTick > tick) continue;
            if (best == entries_.size() || e.offTick < entries_[best].offTick ||
                (e.offTick == entries_[best].offTick && e.sequence < entries_[best].sequence))
                best = i;
        }
        if (best == entries_.size()) return;

        const Entry e = entries_[best];
        entries_[best] = entries_.back();
        entries_.pop_back();
        if (--counts_[slot(e.channel, e.note)] == 0) onRelease(Release{e.channel, e.note});
    }
}

template <typename F>
void ActiveNoteRegistry::releaseAll(F&& onRelease) noexcept {
    for (int ch = 1; ch <= 16; ++ch)
        for (int n = 0; n <= 127; ++n) {
            const auto c = *MidiChannel::make(ch);
            const auto note = *MidiNote::make(n);
            auto& count = counts_[slot(c, note)];
            if (count > 0) {
                count = 0;
                onRelease(Release{c, note});
            }
        }
    entries_.clear();
}

} // namespace signalflux::engine
