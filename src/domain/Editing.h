// SignalFlux — document edits and undo/redo (task SF-009; DECISIONS D27).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Every UI edit is a pure function from one valid document to the next. Edits
// keep the document consistent (outgoing order, gate and scene references,
// mappings), so the UI never has to. History stores whole documents: projects
// are small, and snapshots make undo trivially correct.
//
// Thread ownership: message thread.

#pragma once

#include "domain/Project.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace signalflux::editing {

// --- ID allocation: one past the largest id of each kind in the document.
[[nodiscard]] StationId nextStationId(const ProjectDocument& doc);
[[nodiscard]] FlowpointId nextFlowpointId(const ProjectDocument& doc);
[[nodiscard]] GateId nextGateId(const ProjectDocument& doc);
[[nodiscard]] SwitchId nextSwitchId(const ProjectDocument& doc);
[[nodiscard]] SceneId nextSceneId(const ProjectDocument& doc);
[[nodiscard]] MappingId nextMappingId(const ProjectDocument& doc);

[[nodiscard]] Station* findStation(ProjectDocument& doc, StationId id);
[[nodiscard]] const Station* findStation(const ProjectDocument& doc, StationId id);
[[nodiscard]] const Station* stationAt(const ProjectDocument& doc, GridCoordinate cell);
[[nodiscard]] Flowpoint* findFlowpoint(ProjectDocument& doc, FlowpointId id);

/// Adds a Station at `cell` if it is free and inside the Plot. Colour cycles through the palette.
std::optional<StationId> addStation(ProjectDocument& doc, GridCoordinate cell);
/// Moves a Station if the target cell is free and inside the Plot.
bool moveStation(ProjectDocument& doc, StationId id, GridCoordinate cell);
/// Deletes a Station and everything that refers to it: its routes (and their
/// entries in outgoing orders), gate inputs and targets, scene mutes, mappings.
bool deleteStation(ProjectDocument& doc, StationId id);

/// Connects two Stations (not itself; not a duplicate of an existing route).
std::optional<FlowpointId> connect(ProjectDocument& doc, StationId from, StationId to);
bool deleteFlowpoint(ProjectDocument& doc, FlowpointId id);

std::optional<GateId> addGate(ProjectDocument& doc, GridCoordinate cell, GateType type);
bool deleteGate(ProjectDocument& doc, GateId id);
std::optional<SwitchId> addSwitch(ProjectDocument& doc, GridCoordinate cell);
bool deleteSwitch(ProjectDocument& doc, SwitchId id);

/// Scenes (D35): store the current manual mutes, Switch states and tempo.
SceneId storeScene(ProjectDocument& doc, std::optional<SceneId> into, std::string name);
/// The document with the scene's mutes, Switch states and tempo applied.
[[nodiscard]] ProjectDocument withSceneApplied(const ProjectDocument& doc, SceneId scene);

/// Adds a learned mapping, replacing any mapping with the same source (D30).
MappingId addMapping(ProjectDocument& doc, MidiMapping mapping);

/// Undo/redo of whole documents with gesture coalescing (D27).
class History {
public:
    explicit History(std::size_t limit = 500) : limit_(limit) {}

    /// Starts a history at `doc` (new or opened project).
    void reset(const ProjectDocument& doc);

    /// Records `doc` as the result of an edit called `label`. Successive edits
    /// with the same non-empty `gestureKey` (one drag, one knob turn) merge into
    /// a single undo step.
    void commit(const ProjectDocument& doc, std::string label, std::string gestureKey = {});
    /// Replaces the current step's document without creating an undo step
    /// (performance actions such as Switch flips and Scene recall, D27).
    void replaceCurrent(const ProjectDocument& doc) {
        if (steps_.empty()) reset(doc);
        else steps_[index_].doc = doc;
    }
    /// Ends the current gesture so the next edit starts a new step.
    void endGesture() { gesture_.clear(); }

    [[nodiscard]] bool canUndo() const noexcept { return index_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return index_ + 1 < steps_.size(); }
    [[nodiscard]] std::string undoLabel() const { return canUndo() ? steps_[index_].label : std::string{}; }
    [[nodiscard]] std::string redoLabel() const { return canRedo() ? steps_[index_ + 1].label : std::string{}; }

    const ProjectDocument& undo();
    const ProjectDocument& redo();
    [[nodiscard]] const ProjectDocument& current() const { return steps_[index_].doc; }
    [[nodiscard]] std::size_t size() const noexcept { return steps_.size(); }

private:
    struct Step {
        ProjectDocument doc;
        std::string label;
    };
    std::deque<Step> steps_;
    std::size_t index_ = 0;
    std::size_t limit_;
    std::string gesture_;
};

} // namespace signalflux::editing
