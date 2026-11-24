// SignalFlux — helpers for building test projects.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#pragma once

#include "domain/Project.h"
#include "engine/EventLog.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace signalflux::test {

class ProjectBuilder {
public:
    ProjectBuilder() : doc_(makeDefaultProject()) {}

    ProjectBuilder& station(std::uint32_t id, GridCoordinate pos, int note = 60, bool start = false) {
        Station s;
        s.id = StationId{id};
        s.position = pos;
        s.isStart = start;
        s.note.absoluteNote = *MidiNote::make(note);
        plot().stations.push_back(s);
        return *this;
    }

    ProjectBuilder& route(std::uint32_t id, std::uint32_t from, std::uint32_t to) {
        Flowpoint f;
        f.id = FlowpointId{id};
        f.from = StationId{from};
        f.to = StationId{to};
        plot().flowpoints.push_back(f);
        get(from).outgoingOrder.push_back(f.id);
        return *this;
    }

    Station& get(std::uint32_t id) {
        return *std::find_if(plot().stations.begin(), plot().stations.end(),
                             [id](const Station& s) { return s.id.value() == id; });
    }

    ProjectSettings& settings() { return doc_.settings; }
    ProjectDocument& doc() { return doc_; }

private:
    Plot& plot() { return doc_.plots.front(); }
    ProjectDocument doc_;
};

/// Removes a Flowpoint and its entry in the source Station's outgoing order.
inline void removeFlowpoint(ProjectDocument& doc, std::uint32_t id) {
    auto& plot = doc.plots.front();
    std::erase_if(plot.flowpoints, [id](const Flowpoint& f) { return f.id.value() == id; });
    for (auto& s : plot.stations) std::erase_if(s.outgoingOrder, [id](FlowpointId f) { return f.value() == id; });
}

/// Removes a Station and every Flowpoint touching it.
inline void removeStation(ProjectDocument& doc, std::uint32_t id) {
    auto& plot = doc.plots.front();
    std::vector<std::uint32_t> doomed;
    for (const auto& f : plot.flowpoints)
        if (f.from.value() == id || f.to.value() == id) doomed.push_back(f.id.value());
    for (const auto f : doomed) removeFlowpoint(doc, f);
    std::erase_if(plot.stations, [id](const Station& s) { return s.id.value() == id; });
}

inline Station& stationIn(ProjectDocument& doc, std::uint32_t id) {
    auto& st = doc.plots.front().stations;
    return *std::find_if(st.begin(), st.end(), [id](const Station& s) { return s.id.value() == id; });
}

/// Canonical lines of a log, for readable assertions.
inline std::vector<std::string> lines(const engine::EventLog& log) {
    std::vector<std::string> out;
    for (const auto& e : log.events()) out.push_back(engine::toCanonicalString(e));
    return out;
}

inline bool contains(const engine::EventLog& log, const std::string& line) {
    const auto all = lines(log);
    return std::find(all.begin(), all.end(), line) != all.end();
}

inline std::size_t count(const engine::EventLog& log, engine::EngineEventType type) {
    return static_cast<std::size_t>(std::count_if(log.events().begin(), log.events().end(),
                                                  [type](const engine::EngineEvent& e) { return e.type == type; }));
}

} // namespace signalflux::test
