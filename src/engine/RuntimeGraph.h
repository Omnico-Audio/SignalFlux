// SignalFlux — compiled runtime graph (task SF-003).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// An immutable, validated, index-based view of a ProjectDocument that the
// scheduler reads. It holds only what sequencing needs; it is rebuilt on every
// document change and never mutated afterwards.
//
// Thread ownership: built and owned by the engine/control thread. Immutable
// after compile(), so it may be shared read-only.

#pragma once

#include "domain/Project.h"
#include "domain/Validation.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace signalflux::engine {

using StationIndex = std::uint32_t;
using FlowpointIndex = std::uint32_t;

struct RuntimeStation {
    StationId id;
    bool isStart = false;
    bool manuallyMuted = false;
    StationEventType eventType = StationEventType::Note;
    MidiChannel channel;
    NoteSettings note;
    BranchMode branchMode = BranchMode::Sequential;
    std::vector<FlowpointIndex> outgoing; ///< In the Station's explicit outgoing order (D6).
};

struct RuntimeFlowpoint {
    FlowpointId id;
    StationIndex from = 0;
    StationIndex to = 0;
    MusicalTick duration;                 ///< Leg duration from current geometry (D2, D33).
};

class RuntimeGraph {
public:
    /// Validates and compiles. Returns nullopt if the document has validation
    /// errors; the report (if requested) explains why.
    [[nodiscard]] static std::optional<RuntimeGraph> compile(const ProjectDocument& doc,
                                                             ValidationReport* report = nullptr);

    [[nodiscard]] const std::vector<RuntimeStation>& stations() const noexcept { return stations_; }
    [[nodiscard]] const std::vector<RuntimeFlowpoint>& flowpoints() const noexcept { return flowpoints_; }
    [[nodiscard]] const ProjectSettings& settings() const noexcept { return settings_; }

    [[nodiscard]] std::optional<StationIndex> stationIndex(StationId id) const noexcept;
    [[nodiscard]] std::optional<FlowpointIndex> flowpointIndex(FlowpointId id) const noexcept;

    /// Start Stations in ascending StationId order (D14: Starter spawn order).
    [[nodiscard]] const std::vector<StationIndex>& startStations() const noexcept { return startStations_; }

private:
    RuntimeGraph() = default;

    std::vector<RuntimeStation> stations_;
    std::vector<RuntimeFlowpoint> flowpoints_;
    std::vector<StationIndex> startStations_;
    // Sorted (id, index) tables: deterministic lookup without unordered containers (D25).
    std::vector<std::pair<std::uint32_t, StationIndex>> stationLookup_;
    std::vector<std::pair<std::uint32_t, FlowpointIndex>> flowpointLookup_;
    ProjectSettings settings_;
};

} // namespace signalflux::engine
