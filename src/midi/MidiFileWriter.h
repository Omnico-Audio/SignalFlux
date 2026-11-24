// SignalFlux — Standard MIDI File export for Capture and Render (DECISIONS D28).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Type 1 file, 960 PPQ: track 0 holds tempo and text meta-events (project name,
// seed); one track per MIDI channel used, in channel order. Event ticks are
// shifted so the first captured event lands at tick 0. Pure function: no I/O.

#pragma once

#include "domain/Types.h"
#include "engine/EngineEvent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace signalflux::midi {

struct MidiFileInfo {
    std::string projectName;
    std::uint64_t seed = 0;
    MilliBpm tempo;
};

[[nodiscard]] std::vector<std::uint8_t> writeMidiFile(const std::vector<engine::EngineEvent>& events,
                                                      const MidiFileInfo& info);

} // namespace signalflux::midi
