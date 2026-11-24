// SignalFlux — document edits and undo/redo (D27).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Editing.h"

#include <algorithm>
#include <variant>

namespace signalflux::editing {

namespace {

Plot& plot(ProjectDocument& doc) { return doc.plots.front(); }
const Plot& plot(const ProjectDocument& doc) { return doc.plots.front(); }

template <typename Range, typename Get>
std::uint32_t maxId(const Range& r, Get get) {
    std::uint32_t m = 0;
    for (const auto& x : r) m = std::max(m, get(x));
    return m;
}

bool inside(const Plot& p, GridCoordinate c) { return c.x >= 0 && c.y >= 0 && c.x < p.width && c.y < p.height; }

bool cellFree(const ProjectDocument& doc, GridCoordinate c) { return stationAt(doc, c) == nullptr; }

} // namespace

StationId nextStationId(const ProjectDocument& doc) {
    return StationId{maxId(plot(doc).stations, [](const Station& s) { return s.id.value(); }) + 1};
}
FlowpointId nextFlowpointId(const ProjectDocument& doc) {
    return FlowpointId{maxId(plot(doc).flowpoints, [](const Flowpoint& f) { return f.id.value(); }) + 1};
}
GateId nextGateId(const ProjectDocument& doc) {
    return GateId{maxId(plot(doc).gates, [](const Gate& g) { return g.id.value(); }) + 1};
}
SwitchId nextSwitchId(const ProjectDocument& doc) {
    return SwitchId{maxId(plot(doc).switches, [](const Switch& s) { return s.id.value(); }) + 1};
}
SceneId nextSceneId(const ProjectDocument& doc) {
    return SceneId{maxId(doc.scenes, [](const Scene& s) { return s.id.value(); }) + 1};
}
MappingId nextMappingId(const ProjectDocument& doc) {
    return MappingId{maxId(doc.midi.mappings, [](const MidiMapping& m) { return m.id.value(); }) + 1};
}

Station* findStation(ProjectDocument& doc, StationId id) {
    for (auto& s : plot(doc).stations)
        if (s.id == id) return &s;
    return nullptr;
}
const Station* findStation(const ProjectDocument& doc, StationId id) {
    for (const auto& s : plot(doc).stations)
        if (s.id == id) return &s;
    return nullptr;
}
const Station* stationAt(const ProjectDocument& doc, GridCoordinate cell) {
    for (const auto& s : plot(doc).stations)
        if (s.position == cell) return &s;
    return nullptr;
}
Flowpoint* findFlowpoint(ProjectDocument& doc, FlowpointId id) {
    for (auto& f : plot(doc).flowpoints)
        if (f.id == id) return &f;
    return nullptr;
}

std::optional<StationId> addStation(ProjectDocument& doc, GridCoordinate cell) {
    if (!inside(plot(doc), cell) || !cellFree(doc, cell)) return std::nullopt;
    Station s;
    s.id = nextStationId(doc);
    s.name = "Station " + std::to_string(s.id.value());
    s.position = cell;
    s.colour = static_cast<std::uint8_t>((s.id.value() - 1) % limits::kPaletteSize);
    s.isStart = plot(doc).stations.empty(); // the first Station starts, so a new patch plays straight away
    plot(doc).stations.push_back(s);
    return s.id;
}

bool moveStation(ProjectDocument& doc, StationId id, GridCoordinate cell) {
    auto* s = findStation(doc, id);
    if (s == nullptr || !inside(plot(doc), cell)) return false;
    if (s->position == cell) return true;
    if (!cellFree(doc, cell)) return false;
    s->position = cell;
    return true;
}

bool deleteFlowpoint(ProjectDocument& doc, FlowpointId id) {
    auto& fps = plot(doc).flowpoints;
    const auto before = fps.size();
    std::erase_if(fps, [id](const Flowpoint& f) { return f.id == id; });
    for (auto& s : plot(doc).stations) std::erase(s.outgoingOrder, id);
    return fps.size() != before;
}

bool deleteStation(ProjectDocument& doc, StationId id) {
    if (findStation(doc, id) == nullptr) return false;
    std::vector<FlowpointId> doomed;
    for (const auto& f : plot(doc).flowpoints)
        if (f.from == id || f.to == id) doomed.push_back(f.id);
    for (const auto f : doomed) deleteFlowpoint(doc, f);
    std::erase_if(plot(doc).stations, [id](const Station& s) { return s.id == id; });
    for (auto& g : plot(doc).gates) {
        std::erase(g.targets, id);
        std::erase_if(g.inputs, [id](const GateInput& in) {
            const auto* sa = std::get_if<StationActiveInput>(&in);
            return sa != nullptr && sa->station == id;
        });
    }
    for (auto& sc : doc.scenes) std::erase(sc.mutedStations, id);
    std::erase_if(doc.midi.mappings, [id](const MidiMapping& m) { return m.station == id; });
    return true;
}

std::optional<FlowpointId> connect(ProjectDocument& doc, StationId from, StationId to) {
    if (from == to || findStation(doc, from) == nullptr || findStation(doc, to) == nullptr) return std::nullopt;
    for (const auto& f : plot(doc).flowpoints)
        if (f.from == from && f.to == to) return std::nullopt;
    Flowpoint f;
    f.id = nextFlowpointId(doc);
    f.from = from;
    f.to = to;
    plot(doc).flowpoints.push_back(f);
    findStation(doc, from)->outgoingOrder.push_back(f.id);
    return f.id;
}

std::optional<GateId> addGate(ProjectDocument& doc, GridCoordinate cell, GateType type) {
    if (!inside(plot(doc), cell)) return std::nullopt;
    Gate g;
    g.id = nextGateId(doc);
    g.name = "Gate " + std::to_string(g.id.value());
    g.position = cell;
    g.type = type;
    plot(doc).gates.push_back(std::move(g));
    return plot(doc).gates.back().id;
}

bool deleteGate(ProjectDocument& doc, GateId id) {
    auto& gates = plot(doc).gates;
    const auto before = gates.size();
    std::erase_if(gates, [id](const Gate& g) { return g.id == id; });
    for (auto& g : gates)
        std::erase_if(g.inputs, [id](const GateInput& in) {
            const auto* go = std::get_if<GateOutputInput>(&in);
            return go != nullptr && go->gate == id;
        });
    return gates.size() != before;
}

std::optional<SwitchId> addSwitch(ProjectDocument& doc, GridCoordinate cell) {
    if (!inside(plot(doc), cell)) return std::nullopt;
    Switch s;
    s.id = nextSwitchId(doc);
    s.name = "Switch " + std::to_string(s.id.value());
    s.position = cell;
    plot(doc).switches.push_back(s);
    return s.id;
}

bool deleteSwitch(ProjectDocument& doc, SwitchId id) {
    auto& sw = plot(doc).switches;
    const auto before = sw.size();
    std::erase_if(sw, [id](const Switch& s) { return s.id == id; });
    for (auto& g : plot(doc).gates)
        std::erase_if(g.inputs, [id](const GateInput& in) {
            const auto* si = std::get_if<SwitchInput>(&in);
            return si != nullptr && si->sw == id;
        });
    for (auto& sc : doc.scenes) std::erase_if(sc.switchStates, [id](const SwitchState& s) { return s.sw == id; });
    return sw.size() != before;
}

SceneId storeScene(ProjectDocument& doc, std::optional<SceneId> into, std::string name) {
    Scene sc;
    sc.id = into.value_or(nextSceneId(doc));
    sc.name = std::move(name);
    for (const auto& s : plot(doc).stations)
        if (s.manuallyMuted) sc.mutedStations.push_back(s.id);
    for (const auto& sw : plot(doc).switches) sc.switchStates.push_back(SwitchState{sw.id, sw.on});
    sc.tempo = doc.settings.tempo;
    for (auto& existing : doc.scenes)
        if (existing.id == sc.id) {
            existing = std::move(sc);
            return existing.id;
        }
    doc.scenes.push_back(std::move(sc));
    return doc.scenes.back().id;
}

ProjectDocument withSceneApplied(const ProjectDocument& doc, SceneId id) {
    ProjectDocument out = doc;
    const Scene* sc = nullptr;
    for (const auto& s : doc.scenes)
        if (s.id == id) sc = &s;
    if (sc == nullptr) return out;
    for (auto& s : plot(out).stations)
        s.manuallyMuted = std::find(sc->mutedStations.begin(), sc->mutedStations.end(), s.id) != sc->mutedStations.end();
    for (const auto& state : sc->switchStates)
        for (auto& sw : plot(out).switches)
            if (sw.id == state.sw) sw.on = state.on;
    if (sc->tempo) out.settings.tempo = *sc->tempo;
    return out;
}

MappingId addMapping(ProjectDocument& doc, MidiMapping mapping) {
    std::erase_if(doc.midi.mappings, [&](const MidiMapping& m) { return m.source == mapping.source; });
    mapping.id = nextMappingId(doc);
    doc.midi.mappings.push_back(mapping);
    return mapping.id;
}

// --- History -------------------------------------------------------------------------

void History::reset(const ProjectDocument& doc) {
    steps_.clear();
    steps_.push_back(Step{doc, {}});
    index_ = 0;
    gesture_.clear();
}

void History::commit(const ProjectDocument& doc, std::string label, std::string gestureKey) {
    if (steps_.empty()) {
        reset(doc);
        return;
    }
    while (steps_.size() > index_ + 1) steps_.pop_back(); // a new edit discards the redo branch
    if (!gestureKey.empty() && gestureKey == gesture_ && index_ > 0) {
        steps_[index_].doc = doc; // same gesture: one undo step
        return;
    }
    gesture_ = std::move(gestureKey);
    steps_.push_back(Step{doc, std::move(label)});
    ++index_;
    while (steps_.size() > limit_ + 1) {
        steps_.pop_front();
        --index_;
    }
}

const ProjectDocument& History::undo() {
    gesture_.clear();
    if (canUndo()) --index_;
    return steps_[index_].doc;
}

const ProjectDocument& History::redo() {
    gesture_.clear();
    if (canRedo()) ++index_;
    return steps_[index_].doc;
}

} // namespace signalflux::editing
