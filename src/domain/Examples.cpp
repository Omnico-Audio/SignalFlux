// SignalFlux — built-in example projects (D38).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Examples.h"

#include <cstdint>
#include <string>

namespace signalflux::examples {

namespace {

class Builder {
public:
    explicit Builder(std::string name) {
        doc_ = makeDefaultProject();
        doc_.metadata.name = std::move(name);
    }

    Station& station(std::uint32_t id, GridCoordinate pos, int note, bool start = false, int channel = 1) {
        Station s;
        s.id = StationId{id};
        s.name = "S" + std::to_string(id);
        s.position = pos;
        s.isStart = start;
        s.note.absoluteNote = *MidiNote::make(note);
        s.channel = *MidiChannel::make(channel);
        plot().stations.push_back(std::move(s));
        return plot().stations.back();
    }

    void route(std::uint32_t id, std::uint32_t from, std::uint32_t to, RouteRate rate = RouteRate{}) {
        Flowpoint f;
        f.id = FlowpointId{id};
        f.from = StationId{from};
        f.to = StationId{to};
        f.rate = rate;
        plot().flowpoints.push_back(f);
        for (auto& s : plot().stations)
            if (s.id == f.from) s.outgoingOrder.push_back(f.id);
    }

    Station& get(std::uint32_t id) {
        for (auto& s : plot().stations)
            if (s.id.value() == id) return s;
        return plot().stations.front();
    }

    ProjectDocument done() { return std::move(doc_); }

private:
    Plot& plot() { return doc_.plots.front(); }
    ProjectDocument doc_;
};

/// Two Stations, one route each way: the simplest possible patch.
ProjectDocument firstLoop() {
    Builder b{"First Loop"};
    b.station(1, {0, 0}, 60, true);
    b.station(2, {2, 0}, 64);
    b.route(1, 1, 2);
    b.route(2, 2, 1);
    return b.done();
}

/// A Sequential hub visiting three spokes in turn (D6).
ProjectDocument sequentialHub() {
    Builder b{"Sequential Hub"};
    b.station(1, {4, 4}, 48, true);
    b.station(2, {4, 2}, 60);
    b.station(3, {6, 4}, 64);
    b.station(4, {4, 5}, 67);
    for (std::uint32_t spoke = 2; spoke <= 4; ++spoke) {
        b.route(spoke * 10, 1, spoke);
        b.route(spoke * 10 + 1, spoke, 1);
    }
    return b.done();
}

/// Instant branching inside a loop: Travelers double every cycle until the
/// default cap of 8 starts dropping them (D23).
ProjectDocument instantBloom() {
    Builder b{"Instant Bloom"};
    b.station(1, {0, 0}, 60, true).branchMode = BranchMode::Instant;
    b.station(2, {2, 0}, 67, false, 2);
    b.station(3, {0, 1}, 55, false, 3);
    b.route(1, 1, 2);
    b.route(2, 1, 3);
    b.route(3, 2, 1);
    b.route(4, 3, 1);
    return b.done();
}

/// Two loops at different route speeds (D33): one note per beat against one
/// every 2/3 beat, i.e. 3 against 2.
ProjectDocument polyrhythm() {
    Builder b{"Polyrhythm"};
    b.station(1, {0, 0}, 36, true, 10);
    b.station(2, {1, 0}, 38, false, 10);
    b.route(1, 1, 2);
    b.route(2, 2, 1);
    b.station(3, {0, 3}, 72, true);
    b.station(4, {1, 3}, 79);
    b.route(3, 3, 4, *RouteRate::make(3, 2)); // 1 cell at x3/2 = 640 ticks
    b.route(4, 4, 3, *RouteRate::make(3, 2));
    b.get(3).note.duration = MusicalTick{240};
    b.get(4).note.duration = MusicalTick{240};
    return b.done();
}

/// A bounded random walk over A minor pentatonic (D10 Walk mode).
ProjectDocument wanderer() {
    Builder b{"Wanderer"};
    b.station(1, {0, 0}, 57, true);
    b.station(2, {1, 0}, 57);
    b.station(3, {1, 1}, 57);
    b.route(1, 1, 2);
    b.route(2, 2, 3);
    b.route(3, 3, 1);
    for (std::uint32_t id = 1; id <= 3; ++id) {
        auto& n = b.get(id).note;
        n.mode = NoteMode::RandomValue;
        n.random.mode = RandomValueMode::Walk;
        n.random.anchor = MidiNote::of<69>();
        n.random.rangeSteps = id == 3 ? 3 : 1;
        n.random.boundSteps = 6;
        n.duration = MusicalTick{720};
    }
    ProjectDocument doc = b.done();
    doc.settings.root = PitchClass::of<9>();
    doc.settings.scale = maskFor(ScalePreset::MinorPentatonic);
    doc.settings.randomSeed = 2026;
    doc.settings.seedMode = SeedMode::Fixed;
    return doc;
}

struct Entry {
    std::string_view name;
    ProjectDocument (*make)();
};

constexpr Entry kExamples[] = {
    {"first-loop", firstLoop},
    {"sequential-hub", sequentialHub},
    {"instant-bloom", instantBloom},
    {"polyrhythm", polyrhythm},
    {"wanderer", wanderer},
};

} // namespace

std::vector<std::string_view> names() {
    std::vector<std::string_view> out;
    for (const auto& e : kExamples) out.push_back(e.name);
    return out;
}

std::optional<ProjectDocument> make(std::string_view name) {
    for (const auto& e : kExamples)
        if (e.name == name) return e.make();
    return std::nullopt;
}

} // namespace signalflux::examples
