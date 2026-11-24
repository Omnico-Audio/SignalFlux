// SignalFlux — compiled runtime graph (task SF-003).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/RuntimeGraph.h"

#include "domain/Geometry.h"

#include <algorithm>

namespace signalflux::engine {

namespace {

template <typename Index>
std::optional<Index> lookup(const std::vector<std::pair<std::uint32_t, Index>>& table, std::uint32_t id) noexcept {
    const auto it = std::lower_bound(table.begin(), table.end(), id,
                                     [](const auto& entry, std::uint32_t key) { return entry.first < key; });
    if (it == table.end() || it->first != id) return std::nullopt;
    return it->second;
}

} // namespace

std::optional<RuntimeGraph> RuntimeGraph::compile(const ProjectDocument& doc, ValidationReport* report) {
    auto validation = validateProject(doc);
    const bool ok = validation.ok();
    if (report != nullptr) *report = std::move(validation);
    if (!ok) return std::nullopt;

    const Plot& plot = doc.plots.front(); // validation guarantees exactly one (D1)
    RuntimeGraph g;
    g.settings_ = doc.settings;

    g.stations_.reserve(plot.stations.size());
    for (std::size_t i = 0; i < plot.stations.size(); ++i) {
        const Station& s = plot.stations[i];
        RuntimeStation rs;
        rs.id = s.id;
        rs.isStart = s.isStart;
        rs.manuallyMuted = s.manuallyMuted;
        rs.eventType = s.eventType;
        rs.channel = s.channel;
        rs.note = s.note;
        rs.branchMode = s.branchMode;
        g.stations_.push_back(std::move(rs));
        g.stationLookup_.emplace_back(s.id.value(), static_cast<StationIndex>(i));
    }
    std::sort(g.stationLookup_.begin(), g.stationLookup_.end());

    g.flowpoints_.reserve(plot.flowpoints.size());
    for (std::size_t i = 0; i < plot.flowpoints.size(); ++i) {
        const Flowpoint& f = plot.flowpoints[i];
        const StationIndex from = *lookup(g.stationLookup_, f.from.value());
        const StationIndex to = *lookup(g.stationLookup_, f.to.value());
        const auto length = manhattanLength(plot.stations[from].position, plot.stations[to].position);
        const auto duration = legDuration(length, doc.settings.gridSpacing, f.rate);
        if (!duration) return std::nullopt; // unreachable after validation; defensive
        g.flowpoints_.push_back(RuntimeFlowpoint{f.id, from, to, *duration});
        g.flowpointLookup_.emplace_back(f.id.value(), static_cast<FlowpointIndex>(i));
    }
    std::sort(g.flowpointLookup_.begin(), g.flowpointLookup_.end());

    for (std::size_t i = 0; i < plot.stations.size(); ++i)
        for (const FlowpointId fid : plot.stations[i].outgoingOrder)
            g.stations_[i].outgoing.push_back(*lookup(g.flowpointLookup_, fid.value()));

    for (const auto& [id, index] : g.stationLookup_) // ascending StationId
        if (g.stations_[index].isStart) g.startStations_.push_back(index);

    return g;
}

std::optional<StationIndex> RuntimeGraph::stationIndex(StationId id) const noexcept {
    return lookup(stationLookup_, id.value());
}

std::optional<FlowpointIndex> RuntimeGraph::flowpointIndex(FlowpointId id) const noexcept {
    return lookup(flowpointLookup_, id.value());
}

} // namespace signalflux::engine
