// SignalFlux — project validation (task SF-002).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Strong types already make out-of-range MIDI values unrepresentable. This
// module checks everything strong types cannot: plain-integer ranges, unique
// IDs, graph references, and cross-object rules from DECISIONS.
//
// Issues are reported in a deterministic order (document order), so reports
// are identical across platforms and can be compared in tests.

#pragma once

#include "domain/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace signalflux {

enum class Severity : std::uint8_t { Error, Warning };

enum class IssueCode : std::uint16_t {
    // Document structure
    UnsupportedSchemaVersion,
    PlotCountNotOne,
    TooManyScenes,
    TooManyOutputs,
    // Identity
    InvalidId,
    DuplicateId,
    // Geometry
    PlotSizeOutOfRange,
    StationOutOfBounds,
    StationCellOccupied,
    // Flowpoints
    FlowpointUnknownStation,
    FlowpointSelfLoop,
    FlowpointWaypointsUnsupported,
    OutgoingOrderMismatch,
    RouteRateInexact,
    // Station parameters
    ValueOutOfRange,
    ChordInvalid,
    // Logic
    GateArity,
    GateUnknownReference,
    StationMultipleDrivers,
    // Scenes and mappings
    SceneUnknownReference,
    MappingMissingTarget,
    MappingUnknownReference,
    // Warnings
    NoStartStation,
    AllRandomWeightsZero,
    PassOverridesPassThrough,
    PassThroughIgnoredOutsideRelative,
    ForceScaleIgnoredOutsideAbsolute,
    GateUnconnected,
};

struct ValidationIssue {
    Severity severity = Severity::Error;
    IssueCode code = IssueCode::ValueOutOfRange;
    std::string path;      ///< e.g. "plots[0].stations[2].note.duration"
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    /// True if there are no errors (warnings allowed).
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool has(IssueCode code) const noexcept;
    [[nodiscard]] std::size_t errorCount() const noexcept;
    [[nodiscard]] std::size_t warningCount() const noexcept;
};

[[nodiscard]] ValidationReport validateProject(const ProjectDocument& doc);

[[nodiscard]] std::string_view toString(IssueCode code) noexcept;

} // namespace signalflux
