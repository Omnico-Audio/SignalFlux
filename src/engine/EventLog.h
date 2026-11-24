// SignalFlux — recording event sink and offline render (D25, D28).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// EventLog allocates as it records, so it is for tests, signalflux-render and
// offline export only, never a real-time sink.

#pragma once

#include "domain/Project.h"
#include "engine/EngineEvent.h"

#include <string>
#include <vector>

namespace signalflux::engine {

class EventLog final : public IEngineEventSink {
public:
    void onEngineEvent(const EngineEvent& event) noexcept override;

    [[nodiscard]] const std::vector<EngineEvent>& events() const noexcept { return events_; }
    void clear() noexcept { events_.clear(); }

    /// Canonical lines joined with '\n', ending with '\n' (D25).
    [[nodiscard]] std::string canonical() const;

private:
    std::vector<EngineEvent> events_;
};

/// Deterministic offline run: Play at tick 0, process up to `length`, request
/// Stop, and process until the Stop takes effect. Returns the canonical log, or
/// an empty string if the document is invalid.
[[nodiscard]] std::string renderCanonicalLog(const ProjectDocument& doc, MusicalTick length);

} // namespace signalflux::engine
