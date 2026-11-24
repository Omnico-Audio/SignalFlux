// SignalFlux — active-note registry.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/ActiveNoteRegistry.h"

#include "domain/Project.h"

#include <algorithm>

namespace signalflux::engine {

void ActiveNoteRegistry::reset(std::size_t capacity) {
    entries_.clear();
    entries_.reserve(std::max<std::size_t>(capacity, limits::kMaxActiveNoteCap));
    counts_.fill(0);
    capacity_ = capacity;
    nextSequence_ = 0;
}

bool ActiveNoteRegistry::add(MidiChannel channel, MidiNote note, MusicalTick offTick, StationId station) noexcept {
    if (entries_.size() >= capacity_) return false;
    entries_.push_back(Entry{channel, note, offTick, nextSequence_++, station}); // within reserved capacity
    ++counts_[slot(channel, note)];
    return true;
}

std::optional<MusicalTick> ActiveNoteRegistry::nextDue() const noexcept {
    std::optional<MusicalTick> best;
    for (const auto& e : entries_)
        if (!best || e.offTick < *best) best = e.offTick;
    return best;
}

} // namespace signalflux::engine
