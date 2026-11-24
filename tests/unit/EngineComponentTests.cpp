// SignalFlux — leg timing policy, active-note registry, runtime graph (SF-003).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/ActiveNoteRegistry.h"
#include "engine/LegTiming.h"
#include "engine/RuntimeGraph.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

using namespace signalflux;
using namespace signalflux::engine;

// --- Destination movement policy (SF-003: "isolated and unit-tested") -------

TEST_CASE("recomputeDeadline keeps elapsed ticks and uses the new length (D3, D4)", "[engine][timing]") {
    // Leg started at 1000; 480 ticks have elapsed at now = 1480.
    const MusicalTick start{1000};
    const MusicalTick now{1480};
    SECTION("longer leg: arrival moves later by exactly the added duration") {
        CHECK(recomputeDeadline(start, MusicalTick{2880}, now) == MusicalTick{3880});
    }
    SECTION("shorter leg, still ahead: arrival moves earlier") {
        CHECK(recomputeDeadline(start, MusicalTick{960}, now) == MusicalTick{1960});
    }
    SECTION("shorter than already travelled: arrive now, never in the past") {
        CHECK(recomputeDeadline(start, MusicalTick{240}, now) == now);
    }
    SECTION("exactly reaching now: arrive now") {
        CHECK(recomputeDeadline(start, MusicalTick{480}, now) == now);
    }
    SECTION("unchanged length is a no-op") {
        CHECK(recomputeDeadline(start, MusicalTick{1920}, now) == MusicalTick{2920});
    }
}

// --- Active-note registry (D22, D23) ----------------------------------------

namespace {
const auto ch1 = MidiChannel::of<1>();
const auto ch2 = MidiChannel::of<2>();
const auto c4 = MidiNote::of<60>();
const auto e4 = MidiNote::of<64>();
} // namespace

TEST_CASE("Overlapping same-pitch notes are reference counted (D22)", "[engine][notes]") {
    ActiveNoteRegistry reg;
    reg.reset(16);
    REQUIRE(reg.add(ch1, c4, MusicalTick{960}));
    REQUIRE(reg.add(ch1, c4, MusicalTick{1440}));
    CHECK(reg.soundingCount(ch1, c4) == 2);

    int released = 0;
    reg.releaseDue(MusicalTick{960}, [&](const ActiveNoteRegistry::Release& r) { released += r.sendNoteOff ? 1 : 0; });
    CHECK(released == 0); // first instance ended, but the note is still sounding
    CHECK(reg.soundingCount(ch1, c4) == 1);

    reg.releaseDue(MusicalTick{1440}, [&](const ActiveNoteRegistry::Release& r) {
        REQUIRE(r.sendNoteOff);
        ++released;
        CHECK(r.channel == ch1);
        CHECK(r.note == c4);
    });
    CHECK(released == 1);
    CHECK(reg.size() == 0);
}

TEST_CASE("Same pitch on different channels is independent", "[engine][notes]") {
    ActiveNoteRegistry reg;
    reg.reset(16);
    REQUIRE(reg.add(ch1, c4, MusicalTick{100}));
    REQUIRE(reg.add(ch2, c4, MusicalTick{200}));
    int released = 0;
    reg.releaseDue(MusicalTick{100}, [&](const ActiveNoteRegistry::Release& r) {
        ++released;
        CHECK(r.channel == ch1);
    });
    CHECK(released == 1);
}

TEST_CASE("Due notes release in (offTick, insertion) order", "[engine][notes]") {
    ActiveNoteRegistry reg;
    reg.reset(16);
    REQUIRE(reg.add(ch1, e4, MusicalTick{300}));
    REQUIRE(reg.add(ch1, c4, MusicalTick{200}));
    REQUIRE(reg.add(ch2, c4, MusicalTick{300}));
    std::vector<std::pair<int, int>> order;
    reg.releaseDue(MusicalTick{1000}, [&](const ActiveNoteRegistry::Release& r) {
        order.emplace_back(r.channel.value(), r.note.value());
    });
    CHECK(order == std::vector<std::pair<int, int>>{{1, 60}, {1, 64}, {2, 60}});
}

TEST_CASE("Cap refuses new notes; lowering the cap keeps sounding notes (D23)", "[engine][notes]") {
    ActiveNoteRegistry reg;
    reg.reset(2);
    CHECK(reg.add(ch1, c4, MusicalTick{10}));
    CHECK(reg.add(ch1, e4, MusicalTick{10}));
    CHECK_FALSE(reg.add(ch2, c4, MusicalTick{10}));
    reg.setCapacity(1);
    CHECK(reg.size() == 2);
    CHECK_FALSE(reg.add(ch2, e4, MusicalTick{10}));
}

TEST_CASE("releaseAll sends one release per sounding pitch, ordered by channel then pitch", "[engine][notes]") {
    ActiveNoteRegistry reg;
    reg.reset(16);
    REQUIRE(reg.add(ch2, c4, MusicalTick{10}));
    REQUIRE(reg.add(ch1, e4, MusicalTick{10}));
    REQUIRE(reg.add(ch1, e4, MusicalTick{20}));
    std::vector<std::pair<int, int>> order;
    reg.releaseAll([&](const ActiveNoteRegistry::Release& r) { order.emplace_back(r.channel.value(), r.note.value()); });
    CHECK(order == std::vector<std::pair<int, int>>{{1, 64}, {2, 60}});
    CHECK(reg.size() == 0);
    CHECK_FALSE(reg.nextDue());
}

// --- Runtime graph -----------------------------------------------------------

TEST_CASE("RuntimeGraph compiles leg durations from Manhattan geometry", "[engine][graph]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {3, 2}).route(10, 1, 2).route(11, 2, 1);
    const auto g = RuntimeGraph::compile(b.doc());
    REQUIRE(g);
    CHECK(g->flowpoints()[*g->flowpointIndex(FlowpointId{10})].duration == MusicalTick{4800});

    b.settings().gridSpacing = *GridSpacing::make(480);
    CHECK(RuntimeGraph::compile(b.doc())->flowpoints()[0].duration == MusicalTick{2400});
}

TEST_CASE("RuntimeGraph lists Start Stations in ascending id order", "[engine][graph]") {
    test::ProjectBuilder b;
    b.station(9, {0, 0}, 60, true).station(2, {1, 0}, 60, true).station(5, {2, 0}, 60, true);
    const auto g = RuntimeGraph::compile(b.doc());
    REQUIRE(g);
    std::vector<std::uint32_t> ids;
    for (const auto i : g->startStations()) ids.push_back(g->stations()[i].id.value());
    CHECK(ids == std::vector<std::uint32_t>{2, 5, 9});
}

TEST_CASE("RuntimeGraph keeps the explicit outgoing order (D6)", "[engine][graph]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).station(3, {0, 1});
    b.route(10, 1, 2).route(11, 1, 3);
    b.get(1).outgoingOrder = {FlowpointId{11}, FlowpointId{10}};
    const auto g = RuntimeGraph::compile(b.doc());
    REQUIRE(g);
    const auto& out = g->stations()[*g->stationIndex(StationId{1})].outgoing;
    REQUIRE(out.size() == 2);
    CHECK(g->flowpoints()[out[0]].id == FlowpointId{11});
    CHECK(g->flowpoints()[out[1]].id == FlowpointId{10});
}

TEST_CASE("RuntimeGraph refuses invalid documents", "[engine][graph]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}).station(2, {0, 0}); // same cell
    ValidationReport report;
    CHECK_FALSE(RuntimeGraph::compile(b.doc(), &report));
    CHECK(report.has(IssueCode::StationCellOccupied));
}
