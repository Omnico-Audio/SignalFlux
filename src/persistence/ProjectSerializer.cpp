// SignalFlux — project files (task SF-010).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "persistence/ProjectSerializer.h"

#include "domain/Validation.h"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

namespace signalflux::persistence {

using nlohmann::json;

// ---------------------------------------------------------------------------
// Enum <-> string tables (strings keep files readable and stable)
// ---------------------------------------------------------------------------

namespace {

template <typename E, std::size_t N>
using EnumTable = std::array<std::pair<E, const char*>, N>;

constexpr EnumTable<StationEventType, 3> kEventTypes{{
    {StationEventType::Note, "note"}, {StationEventType::ControlChange, "cc"}, {StationEventType::PitchBend, "pitchBend"}}};
constexpr EnumTable<NoteMode, 3> kNoteModes{{
    {NoteMode::Absolute, "absolute"}, {NoteMode::Relative, "relative"}, {NoteMode::RandomValue, "randomValue"}}};
constexpr EnumTable<RandomValueMode, 2> kRandomModes{{{RandomValueMode::Anchor, "anchor"}, {RandomValueMode::Walk, "walk"}}};
constexpr EnumTable<BranchMode, 3> kBranchModes{{
    {BranchMode::Sequential, "sequential"}, {BranchMode::Random, "random"}, {BranchMode::Instant, "instant"}}};
constexpr EnumTable<RouteElbow, 2> kElbows{{
    {RouteElbow::HorizontalFirst, "horizontalFirst"}, {RouteElbow::VerticalFirst, "verticalFirst"}}};
constexpr EnumTable<GateType, 6> kGateTypes{{{GateType::And, "and"}, {GateType::Or, "or"}, {GateType::Xor, "xor"},
                                             {GateType::Not, "not"}, {GateType::Nand, "nand"}, {GateType::Nor, "nor"}}};
constexpr EnumTable<MidiSourceType, 3> kSourceTypes{{
    {MidiSourceType::Note, "note"}, {MidiSourceType::ControlChange, "cc"}, {MidiSourceType::PitchBend, "pitchBend"}}};
constexpr EnumTable<MappingTarget, 5> kMappingTargets{{{MappingTarget::GlobalTranspose, "globalTranspose"},
                                                       {MappingTarget::Tempo, "tempo"},
                                                       {MappingTarget::StationVelocity, "stationVelocity"},
                                                       {MappingTarget::StationProbability, "stationProbability"},
                                                       {MappingTarget::SceneRecall, "sceneRecall"}}};
constexpr EnumTable<SeedMode, 2> kSeedModes{{{SeedMode::Fixed, "fixed"}, {SeedMode::NewEachPlay, "newEachPlay"}}};
constexpr EnumTable<SwingGrid, 2> kSwingGrids{{{SwingGrid::Eighths, "eighths"}, {SwingGrid::Sixteenths, "sixteenths"}}};
constexpr EnumTable<SceneQuantize, 3> kSceneQuantize{{
    {SceneQuantize::Immediate, "immediate"}, {SceneQuantize::NextBeat, "nextBeat"}, {SceneQuantize::NextFourBeats, "nextFourBeats"}}};

template <typename E, std::size_t N>
const char* nameOf(const EnumTable<E, N>& table, E v) {
    for (const auto& [e, n] : table)
        if (e == v) return n;
    return table[0].second;
}

std::string seedToString(std::uint64_t v) {
    char buf[24];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
    (void)ec;
    return std::string(buf, end);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

json coord(GridCoordinate c) { return json{{"x", c.x}, {"y", c.y}}; }

json channelOrAny(const std::optional<MidiChannel>& c) { return c ? json(c->value()) : json(nullptr); }

json writeStation(const Station& s) {
    json note{
        {"mode", nameOf(kNoteModes, s.note.mode)},
        {"absoluteNote", s.note.absoluteNote.value()},
        {"relativeSteps", s.note.relativeSteps},
        {"forceScale", s.note.forceScale},
        {"pass", s.note.pass},
        {"relativePassThrough", s.note.relativePassThrough},
        {"random", {{"mode", nameOf(kRandomModes, s.note.random.mode)},
                    {"anchor", s.note.random.anchor.value()},
                    {"rangeSteps", s.note.random.rangeSteps},
                    {"boundSteps", s.note.random.boundSteps}}},
        {"chordOffsets", s.note.chordOffsets},
        {"velocity", s.note.velocity.value()},
        {"duration", s.note.duration.value()},
    };
    json out{
        {"id", s.id.value()},
        {"name", s.name},
        {"position", coord(s.position)},
        {"isStart", s.isStart},
        {"colour", s.colour},
        {"eventType", nameOf(kEventTypes, s.eventType)},
        {"note", note},
        {"controlChange", {{"controller", s.controlChange.controller.value()},
                           {"target", s.controlChange.target.value()},
                           {"ramp", s.controlChange.ramp}}},
        {"pitchBend", {{"target", s.pitchBend.target.value()}, {"ramp", s.pitchBend.ramp}}},
        {"channel", s.channel.value()},
        {"probability", s.probability.value()},
        {"additionalRepeats", s.additionalRepeats},
        {"repeatInterval", s.repeatInterval ? json(s.repeatInterval->value()) : json(nullptr)},
        {"branchMode", nameOf(kBranchModes, s.branchMode)},
        {"terminatorLimit", s.terminatorLimit ? json(*s.terminatorLimit) : json(nullptr)},
        {"manuallyMuted", s.manuallyMuted},
        {"humanize", {{"velocityRange", s.humanize.velocityRange}, {"maxDelay", s.humanize.maxDelay.value()}}},
    };
    json order = json::array();
    for (const auto id : s.outgoingOrder) order.push_back(id.value());
    out["outgoingOrder"] = order;
    return out;
}

json writeGateInput(const GateInput& in) {
    return std::visit(
        [](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, StationActiveInput>) return {{"kind", "stationActive"}, {"station", v.station.value()}};
            else if constexpr (std::is_same_v<T, GateOutputInput>) return {{"kind", "gate"}, {"gate", v.gate.value()}};
            else if constexpr (std::is_same_v<T, SwitchInput>) return {{"kind", "switch"}, {"switch", v.sw.value()}};
            else if constexpr (std::is_same_v<T, MidiNoteInput>)
                return {{"kind", "midiNote"}, {"channel", channelOrAny(v.channel)}, {"note", v.note.value()}};
            else
                return {{"kind", "midiCc"}, {"channel", channelOrAny(v.channel)}, {"controller", v.controller.value()},
                        {"threshold", v.threshold.value()}};
        },
        in);
}

json writePlot(const Plot& p) {
    json stations = json::array(), flowpoints = json::array(), gates = json::array(), switches = json::array();
    for (const auto& s : p.stations) stations.push_back(writeStation(s));
    for (const auto& f : p.flowpoints) {
        json wp = json::array();
        for (const auto& w : f.waypoints) wp.push_back(coord(w));
        flowpoints.push_back({{"id", f.id.value()}, {"from", f.from.value()}, {"to", f.to.value()},
                              {"elbow", nameOf(kElbows, f.elbow)}, {"waypoints", wp}, {"weight", f.weight.value()},
                              {"rate", {{"num", f.rate.numerator()}, {"den", f.rate.denominator()}}}});
    }
    for (const auto& g : p.gates) {
        json inputs = json::array(), targets = json::array();
        for (const auto& in : g.inputs) inputs.push_back(writeGateInput(in));
        for (const auto t : g.targets) targets.push_back(t.value());
        gates.push_back({{"id", g.id.value()}, {"name", g.name}, {"position", coord(g.position)},
                         {"type", nameOf(kGateTypes, g.type)}, {"inputs", inputs}, {"targets", targets}});
    }
    for (const auto& sw : p.switches)
        switches.push_back({{"id", sw.id.value()}, {"name", sw.name}, {"position", coord(sw.position)}, {"on", sw.on}});
    return {{"id", p.id.value()}, {"name", p.name}, {"width", p.width}, {"height", p.height},
            {"stations", stations}, {"flowpoints", flowpoints}, {"gates", gates}, {"switches", switches}};
}

json writeDevices(const std::vector<MidiDeviceRef>& devices) {
    json out = json::array();
    for (const auto& d : devices) out.push_back({{"name", d.name}, {"identifier", d.identifier}});
    return out;
}

} // namespace

std::string toJson(const ProjectDocument& doc) {
    const auto& st = doc.settings;
    json starred = json::array();
    for (const auto s : st.starredSeeds) starred.push_back(seedToString(s));

    json mappings = json::array();
    for (const auto& m : doc.midi.mappings)
        mappings.push_back({{"id", m.id.value()},
                            {"source", {{"device", m.source.deviceName ? json(*m.source.deviceName) : json(nullptr)},
                                        {"channel", channelOrAny(m.source.channel)},
                                        {"type", nameOf(kSourceTypes, m.source.type)},
                                        {"number", m.source.number}}},
                            {"target", nameOf(kMappingTargets, m.target)},
                            {"station", m.station ? json(m.station->value()) : json(nullptr)},
                            {"scene", m.scene ? json(m.scene->value()) : json(nullptr)},
                            {"rangeSteps", m.rangeSteps}});

    json plots = json::array();
    for (const auto& p : doc.plots) plots.push_back(writePlot(p));

    json scenes = json::array();
    for (const auto& sc : doc.scenes) {
        json muted = json::array(), states = json::array();
        for (const auto s : sc.mutedStations) muted.push_back(s.value());
        for (const auto& ss : sc.switchStates) states.push_back({{"switch", ss.sw.value()}, {"on", ss.on}});
        scenes.push_back({{"id", sc.id.value()}, {"name", sc.name}, {"mutedStations", muted}, {"switchStates", states},
                          {"tempoMilliBpm", sc.tempo ? json(sc.tempo->value()) : json(nullptr)}});
    }

    const json root{
        {"format", kFormatName},
        {"schemaVersion", doc.schemaVersion.value},
        {"metadata", {{"name", doc.metadata.name}, {"author", doc.metadata.author}, {"notes", doc.metadata.notes}}},
        {"settings",
         {{"tempoMilliBpm", st.tempo.value()},
          {"gridSpacingTicks", st.gridSpacing.ticks()},
          {"root", st.root.value()},
          {"scaleMask", st.scale.bits()},
          {"randomSeed", seedToString(st.randomSeed)},
          {"seedMode", nameOf(kSeedModes, st.seedMode)},
          {"starredSeeds", starred},
          {"swingPercent", st.swingPercent},
          {"swingGrid", nameOf(kSwingGrids, st.swingGrid)},
          {"travelerCap", st.travelerCap},
          {"activeNoteCap", st.activeNoteCap},
          {"sceneQuantize", nameOf(kSceneQuantize, st.sceneQuantize)},
          {"linkEnabled", st.linkEnabled},
          {"captureMinutes", st.captureMinutes}}},
        {"midi",
         {{"outputs", writeDevices(doc.midi.outputs)},
          {"virtualOutputPort", doc.midi.virtualOutputPort},
          {"inputs", writeDevices(doc.midi.inputs)},
          {"sendClock", doc.midi.sendClock},
          {"clockWhileStopped", doc.midi.clockWhileStopped},
          {"songPositionBeforeStart", doc.midi.songPositionBeforeStart},
          {"mappings", mappings}}},
        {"plots", plots},
        {"scenes", scenes},
    };
    return root.dump(2) + "\n"; // std::map-backed objects: keys sorted, canonical
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

/// Collects errors with JSON paths instead of throwing, so one bad field does
/// not hide the others.
class Reader {
public:
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    const json* child(const json& obj, const char* key, const std::string& path, bool required = true) {
        if (!obj.is_object()) {
            fail(path, "expected an object");
            return nullptr;
        }
        const auto it = obj.find(key);
        if (it == obj.end()) {
            if (required) fail(path + "." + key, "missing");
            return nullptr;
        }
        return &*it;
    }

    template <typename Int>
    Int integer(const json& obj, const char* key, const std::string& path, Int fallback, bool required = false) {
        const json* v = child(obj, key, path, required);
        if (v == nullptr) return fallback;
        if (!v->is_number_integer()) {
            fail(path + "." + key, "expected an integer");
            return fallback;
        }
        if constexpr (std::is_unsigned_v<Int>) {
            if (v->is_number_unsigned()) {
                const auto u = v->get<std::uint64_t>();
                if (u <= std::numeric_limits<Int>::max()) return static_cast<Int>(u);
            }
        } else {
            const auto i = v->get<std::int64_t>();
            if (i >= std::numeric_limits<Int>::min() && i <= std::numeric_limits<Int>::max()) return static_cast<Int>(i);
        }
        fail(path + "." + key, "integer out of range");
        return fallback;
    }

    bool boolean(const json& obj, const char* key, const std::string& path, bool fallback) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr) return fallback;
        if (!v->is_boolean()) {
            fail(path + "." + key, "expected true or false");
            return fallback;
        }
        return v->get<bool>();
    }

    std::string string(const json& obj, const char* key, const std::string& path, std::string fallback = {}) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr) return fallback;
        if (!v->is_string()) {
            fail(path + "." + key, "expected text");
            return fallback;
        }
        return v->get<std::string>();
    }

    template <typename T>
    T bounded(const json& obj, const char* key, const std::string& path, T fallback = T{}) {
        const auto v = integer<std::int64_t>(obj, key, path, fallback.value());
        if (const auto b = T::make(v)) return *b;
        fail(path + "." + key, "value " + std::to_string(v) + " out of range");
        return fallback;
    }

    template <typename Id>
    Id id(const json& obj, const char* key, const std::string& path) {
        return Id{integer<std::uint32_t>(obj, key, path, 0, true)};
    }

    template <typename E, std::size_t N>
    E enumeration(const json& obj, const char* key, const std::string& path, const EnumTable<E, N>& table, E fallback) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr) return fallback;
        if (v->is_string())
            for (const auto& [e, n] : table)
                if (v->get<std::string>() == n) return e;
        fail(path + "." + key, "unknown value");
        return fallback;
    }

    std::optional<MidiChannel> channelOrAny(const json& obj, const char* key, const std::string& path) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr || v->is_null()) return std::nullopt;
        return bounded<MidiChannel>(obj, key, path);
    }

    GridCoordinate coordinate(const json& obj, const char* key, const std::string& path) {
        const json* v = child(obj, key, path);
        if (v == nullptr) return {};
        const auto p = path + "." + key;
        return GridCoordinate{integer<std::int32_t>(*v, "x", p, 0, true), integer<std::int32_t>(*v, "y", p, 0, true)};
    }

    const json* array(const json& obj, const char* key, const std::string& path) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr) return nullptr;
        if (!v->is_array()) {
            fail(path + "." + key, "expected a list");
            return nullptr;
        }
        return v;
    }

    std::uint64_t seed(const json& obj, const char* key, const std::string& path, std::uint64_t fallback) {
        const json* v = child(obj, key, path, false);
        if (v == nullptr) return fallback;
        if (v->is_number_unsigned()) return v->get<std::uint64_t>();
        if (v->is_string()) {
            const auto s = v->get<std::string>();
            std::uint64_t out = 0;
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
            if (ec == std::errc{} && ptr == s.data() + s.size()) return out;
        }
        fail(path + "." + key, "expected a seed (decimal text)");
        return fallback;
    }

    void fail(const std::string& path, const std::string& message) { errors.push_back(path + ": " + message); }
};

std::string idx(const std::string& base, const char* field, std::size_t i) {
    return base + "." + field + "[" + std::to_string(i) + "]";
}

Station readStation(Reader& r, const json& j, const std::string& p) {
    Station s;
    s.id = r.id<StationId>(j, "id", p);
    s.name = r.string(j, "name", p);
    s.position = r.coordinate(j, "position", p);
    s.isStart = r.boolean(j, "isStart", p, false);
    s.colour = r.integer<std::uint8_t>(j, "colour", p, 0);
    s.eventType = r.enumeration(j, "eventType", p, kEventTypes, StationEventType::Note);
    if (const json* n = r.child(j, "note", p, false)) {
        const auto np = p + ".note";
        s.note.mode = r.enumeration(*n, "mode", np, kNoteModes, NoteMode::Absolute);
        s.note.absoluteNote = r.bounded<MidiNote>(*n, "absoluteNote", np);
        s.note.relativeSteps = r.integer<std::int32_t>(*n, "relativeSteps", np, 0);
        s.note.forceScale = r.boolean(*n, "forceScale", np, false);
        s.note.pass = r.boolean(*n, "pass", np, false);
        s.note.relativePassThrough = r.boolean(*n, "relativePassThrough", np, false);
        if (const json* rv = r.child(*n, "random", np, false)) {
            const auto rp = np + ".random";
            s.note.random.mode = r.enumeration(*rv, "mode", rp, kRandomModes, RandomValueMode::Anchor);
            s.note.random.anchor = r.bounded<MidiNote>(*rv, "anchor", rp);
            s.note.random.rangeSteps = r.integer<std::int32_t>(*rv, "rangeSteps", rp, 2);
            s.note.random.boundSteps = r.integer<std::int32_t>(*rv, "boundSteps", rp, 7);
        }
        if (const json* chord = r.array(*n, "chordOffsets", np))
            for (std::size_t i = 0; i < chord->size(); ++i) {
                if ((*chord)[i].is_number_integer()) s.note.chordOffsets.push_back((*chord)[i].get<std::int32_t>());
                else r.fail(idx(np, "chordOffsets", i), "expected an integer");
            }
        s.note.velocity = r.bounded<Velocity>(*n, "velocity", np);
        s.note.duration = MusicalTick{r.integer<std::int64_t>(*n, "duration", np, 480)};
    }
    if (const json* cc = r.child(j, "controlChange", p, false)) {
        const auto cp = p + ".controlChange";
        s.controlChange.controller = r.bounded<CcNumber>(*cc, "controller", cp);
        s.controlChange.target = r.bounded<CcValue>(*cc, "target", cp);
        s.controlChange.ramp = r.boolean(*cc, "ramp", cp, false);
    }
    if (const json* pb = r.child(j, "pitchBend", p, false)) {
        const auto pp = p + ".pitchBend";
        s.pitchBend.target = r.bounded<PitchBendValue>(*pb, "target", pp);
        s.pitchBend.ramp = r.boolean(*pb, "ramp", pp, false);
    }
    s.channel = r.bounded<MidiChannel>(j, "channel", p);
    s.probability = r.bounded<ProbabilityPercent>(j, "probability", p);
    s.additionalRepeats = r.integer<std::uint32_t>(j, "additionalRepeats", p, 0);
    if (const json* ri = r.child(j, "repeatInterval", p, false); ri != nullptr && !ri->is_null())
        s.repeatInterval = MusicalTick{r.integer<std::int64_t>(j, "repeatInterval", p, 480)};
    s.branchMode = r.enumeration(j, "branchMode", p, kBranchModes, BranchMode::Sequential);
    if (const json* t = r.child(j, "terminatorLimit", p, false); t != nullptr && !t->is_null())
        s.terminatorLimit = r.integer<std::uint32_t>(j, "terminatorLimit", p, 0);
    s.manuallyMuted = r.boolean(j, "manuallyMuted", p, false);
    if (const json* h = r.child(j, "humanize", p, false)) {
        s.humanize.velocityRange = r.integer<std::int32_t>(*h, "velocityRange", p + ".humanize", 0);
        s.humanize.maxDelay = MusicalTick{r.integer<std::int64_t>(*h, "maxDelay", p + ".humanize", 0)};
    }
    if (const json* order = r.array(j, "outgoingOrder", p))
        for (std::size_t i = 0; i < order->size(); ++i) {
            if ((*order)[i].is_number_unsigned()) s.outgoingOrder.push_back(FlowpointId{(*order)[i].get<std::uint32_t>()});
            else r.fail(idx(p, "outgoingOrder", i), "expected a Flowpoint id");
        }
    return s;
}

GateInput readGateInput(Reader& r, const json& j, const std::string& p) {
    const auto kind = r.string(j, "kind", p);
    if (kind == "stationActive") return StationActiveInput{r.id<StationId>(j, "station", p)};
    if (kind == "gate") return GateOutputInput{r.id<GateId>(j, "gate", p)};
    if (kind == "switch") return SwitchInput{r.id<SwitchId>(j, "switch", p)};
    if (kind == "midiNote") return MidiNoteInput{r.channelOrAny(j, "channel", p), r.bounded<MidiNote>(j, "note", p)};
    if (kind == "midiCc")
        return MidiCcInput{r.channelOrAny(j, "channel", p), r.bounded<CcNumber>(j, "controller", p),
                           r.bounded<CcValue>(j, "threshold", p)};
    r.fail(p + ".kind", "unknown gate input '" + kind + "'");
    return SwitchInput{};
}

Plot readPlot(Reader& r, const json& j, const std::string& p) {
    Plot plot;
    plot.id = r.id<PlotId>(j, "id", p);
    plot.name = r.string(j, "name", p, "Plot");
    plot.width = r.integer<std::int32_t>(j, "width", p, 64);
    plot.height = r.integer<std::int32_t>(j, "height", p, 64);
    if (const json* a = r.array(j, "stations", p))
        for (std::size_t i = 0; i < a->size(); ++i) plot.stations.push_back(readStation(r, (*a)[i], idx(p, "stations", i)));
    if (const json* a = r.array(j, "flowpoints", p))
        for (std::size_t i = 0; i < a->size(); ++i) {
            const auto& f = (*a)[i];
            const auto fp = idx(p, "flowpoints", i);
            Flowpoint out;
            out.id = r.id<FlowpointId>(f, "id", fp);
            out.from = r.id<StationId>(f, "from", fp);
            out.to = r.id<StationId>(f, "to", fp);
            out.elbow = r.enumeration(f, "elbow", fp, kElbows, RouteElbow::HorizontalFirst);
            if (const json* w = r.array(f, "waypoints", fp))
                for (std::size_t k = 0; k < w->size(); ++k) {
                    const auto wp = idx(fp, "waypoints", k);
                    out.waypoints.push_back(GridCoordinate{r.integer<std::int32_t>((*w)[k], "x", wp, 0, true),
                                                           r.integer<std::int32_t>((*w)[k], "y", wp, 0, true)});
                }
            out.weight = r.bounded<RouteWeight>(f, "weight", fp);
            if (const json* rate = r.child(f, "rate", fp, false)) {
                const auto num = r.integer<std::int32_t>(*rate, "num", fp + ".rate", 1);
                const auto den = r.integer<std::int32_t>(*rate, "den", fp + ".rate", 1);
                if (const auto rr = RouteRate::make(num, den)) out.rate = *rr;
                else r.fail(fp + ".rate", "rate " + std::to_string(num) + "/" + std::to_string(den) + " is not on the menu");
            }
            plot.flowpoints.push_back(std::move(out));
        }
    if (const json* a = r.array(j, "gates", p))
        for (std::size_t i = 0; i < a->size(); ++i) {
            const auto& g = (*a)[i];
            const auto gp = idx(p, "gates", i);
            Gate out;
            out.id = r.id<GateId>(g, "id", gp);
            out.name = r.string(g, "name", gp);
            out.position = r.coordinate(g, "position", gp);
            out.type = r.enumeration(g, "type", gp, kGateTypes, GateType::And);
            if (const json* in = r.array(g, "inputs", gp))
                for (std::size_t k = 0; k < in->size(); ++k) out.inputs.push_back(readGateInput(r, (*in)[k], idx(gp, "inputs", k)));
            if (const json* t = r.array(g, "targets", gp))
                for (std::size_t k = 0; k < t->size(); ++k) {
                    if ((*t)[k].is_number_unsigned()) out.targets.push_back(StationId{(*t)[k].get<std::uint32_t>()});
                    else r.fail(idx(gp, "targets", k), "expected a Station id");
                }
            plot.gates.push_back(std::move(out));
        }
    if (const json* a = r.array(j, "switches", p))
        for (std::size_t i = 0; i < a->size(); ++i) {
            const auto sp = idx(p, "switches", i);
            plot.switches.push_back(Switch{r.id<SwitchId>((*a)[i], "id", sp), r.string((*a)[i], "name", sp),
                                           r.coordinate((*a)[i], "position", sp), r.boolean((*a)[i], "on", sp, false)});
        }
    return plot;
}

std::vector<MidiDeviceRef> readDevices(Reader& r, const json& j, const char* key, const std::string& p) {
    std::vector<MidiDeviceRef> out;
    if (const json* a = r.array(j, key, p))
        for (std::size_t i = 0; i < a->size(); ++i) {
            const auto dp = idx(p, key, i);
            out.push_back(MidiDeviceRef{r.string((*a)[i], "name", dp), r.string((*a)[i], "identifier", dp)});
        }
    return out;
}

ProjectDocument readDocument(Reader& r, const json& root) {
    ProjectDocument doc;
    doc.plots.clear();
    doc.schemaVersion.value = ProjectSchemaVersion::kCurrent;
    if (const json* m = r.child(root, "metadata", "", false)) {
        doc.metadata.name = r.string(*m, "name", "metadata", "Untitled");
        doc.metadata.author = r.string(*m, "author", "metadata");
        doc.metadata.notes = r.string(*m, "notes", "metadata");
    }
    if (const json* s = r.child(root, "settings", "", false)) {
        const std::string p = "settings";
        auto& st = doc.settings;
        st.tempo = r.bounded<MilliBpm>(*s, "tempoMilliBpm", p);
        const auto spacing = r.integer<std::int64_t>(*s, "gridSpacingTicks", p, kTicksPerQuarter);
        if (const auto g = GridSpacing::make(spacing)) st.gridSpacing = *g;
        else r.fail(p + ".gridSpacingTicks", "not a supported note value");
        st.root = r.bounded<PitchClass>(*s, "root", p);
        if (const auto mask = ScaleMask::make(r.integer<std::uint32_t>(*s, "scaleMask", p, ScaleMask{}.bits()))) st.scale = *mask;
        else r.fail(p + ".scaleMask", "a scale needs 1–12 pitch classes");
        st.randomSeed = r.seed(*s, "randomSeed", p, 1);
        st.seedMode = r.enumeration(*s, "seedMode", p, kSeedModes, SeedMode::NewEachPlay);
        if (const json* a = r.array(*s, "starredSeeds", p))
            for (std::size_t i = 0; i < a->size(); ++i) {
                const json wrapper{{"v", (*a)[i]}};
                st.starredSeeds.push_back(r.seed(wrapper, "v", idx(p, "starredSeeds", i), 0));
            }
        st.swingPercent = r.integer<std::int32_t>(*s, "swingPercent", p, 50);
        st.swingGrid = r.enumeration(*s, "swingGrid", p, kSwingGrids, SwingGrid::Sixteenths);
        st.travelerCap = r.integer<std::uint32_t>(*s, "travelerCap", p, 8);
        st.activeNoteCap = r.integer<std::uint32_t>(*s, "activeNoteCap", p, 256);
        st.sceneQuantize = r.enumeration(*s, "sceneQuantize", p, kSceneQuantize, SceneQuantize::NextBeat);
        st.linkEnabled = r.boolean(*s, "linkEnabled", p, false);
        st.captureMinutes = r.integer<std::uint32_t>(*s, "captureMinutes", p, 10);
    }
    if (const json* m = r.child(root, "midi", "", false)) {
        const std::string p = "midi";
        doc.midi.outputs = readDevices(r, *m, "outputs", p);
        doc.midi.virtualOutputPort = r.boolean(*m, "virtualOutputPort", p, true);
        doc.midi.inputs = readDevices(r, *m, "inputs", p);
        doc.midi.sendClock = r.boolean(*m, "sendClock", p, true);
        doc.midi.clockWhileStopped = r.boolean(*m, "clockWhileStopped", p, false);
        doc.midi.songPositionBeforeStart = r.boolean(*m, "songPositionBeforeStart", p, true);
        if (const json* a = r.array(*m, "mappings", p))
            for (std::size_t i = 0; i < a->size(); ++i) {
                const auto& j = (*a)[i];
                const auto mp = idx(p, "mappings", i);
                MidiMapping mm;
                mm.id = r.id<MappingId>(j, "id", mp);
                if (const json* src = r.child(j, "source", mp)) {
                    const auto sp = mp + ".source";
                    if (const json* d = r.child(*src, "device", sp, false); d != nullptr && d->is_string())
                        mm.source.deviceName = d->get<std::string>();
                    mm.source.channel = r.channelOrAny(*src, "channel", sp);
                    mm.source.type = r.enumeration(*src, "type", sp, kSourceTypes, MidiSourceType::ControlChange);
                    mm.source.number = r.integer<std::int32_t>(*src, "number", sp, 0);
                }
                mm.target = r.enumeration(j, "target", mp, kMappingTargets, MappingTarget::GlobalTranspose);
                if (const json* st = r.child(j, "station", mp, false); st != nullptr && !st->is_null())
                    mm.station = r.id<StationId>(j, "station", mp);
                if (const json* sc = r.child(j, "scene", mp, false); sc != nullptr && !sc->is_null())
                    mm.scene = r.id<SceneId>(j, "scene", mp);
                mm.rangeSteps = r.integer<std::int32_t>(j, "rangeSteps", mp, 12);
                doc.midi.mappings.push_back(std::move(mm));
            }
    }
    if (const json* a = r.array(root, "plots", ""))
        for (std::size_t i = 0; i < a->size(); ++i) doc.plots.push_back(readPlot(r, (*a)[i], "plots[" + std::to_string(i) + "]"));
    if (const json* a = r.array(root, "scenes", ""))
        for (std::size_t i = 0; i < a->size(); ++i) {
            const auto& j = (*a)[i];
            const auto sp = "scenes[" + std::to_string(i) + "]";
            Scene sc;
            sc.id = r.id<SceneId>(j, "id", sp);
            sc.name = r.string(j, "name", sp);
            if (const json* muted = r.array(j, "mutedStations", sp))
                for (const auto& v : *muted)
                    if (v.is_number_unsigned()) sc.mutedStations.push_back(StationId{v.get<std::uint32_t>()});
            if (const json* states = r.array(j, "switchStates", sp))
                for (std::size_t k = 0; k < states->size(); ++k)
                    sc.switchStates.push_back(SwitchState{r.id<SwitchId>((*states)[k], "switch", idx(sp, "switchStates", k)),
                                                          r.boolean((*states)[k], "on", sp, false)});
            if (const json* t = r.child(j, "tempoMilliBpm", sp, false); t != nullptr && !t->is_null())
                sc.tempo = r.bounded<MilliBpm>(j, "tempoMilliBpm", sp);
            doc.scenes.push_back(std::move(sc));
        }
    return doc;
}

} // namespace

const MigrationRegistry& MigrationRegistry::builtIn() {
    static const MigrationRegistry registry; // schema 1 is the first version: nothing to migrate yet
    return registry;
}

LoadResult fromJson(std::string_view text) { return fromJson(text, MigrationRegistry::builtIn()); }

LoadResult fromJson(std::string_view text, const MigrationRegistry& migrations) {
    LoadResult result;
    json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        result.errors.emplace_back("This file isn't a SignalFlux project (it isn't valid JSON).");
        return result;
    }
    if (!root.contains("format") || root["format"] != kFormatName) {
        result.errors.emplace_back("This file isn't a SignalFlux project (missing \"format\": \"signalflux-project\").");
        return result;
    }
    if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_unsigned()) {
        result.errors.emplace_back("The project has no valid schemaVersion.");
        return result;
    }
    auto version = root["schemaVersion"].get<std::uint32_t>();
    result.fileSchemaVersion = version;
    if (version > ProjectSchemaVersion::kCurrent) {
        result.errors.push_back("This project was saved by a newer SignalFlux (file format " + std::to_string(version) +
                                ", this version reads up to " + std::to_string(ProjectSchemaVersion::kCurrent) +
                                "). Update SignalFlux to open it.");
        return result;
    }
    if (version < ProjectSchemaVersion::kCurrent) {
        std::string upgraded{text};
        while (version < ProjectSchemaVersion::kCurrent) {
            if (!migrations.has(version)) {
                result.errors.push_back("No upgrade is available for file format " + std::to_string(version) + ".");
                return result;
            }
            upgraded = migrations.run(version, upgraded);
            ++version;
        }
        root = json::parse(upgraded, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            result.errors.emplace_back("Upgrading the project failed.");
            return result;
        }
        result.warnings.push_back("Upgraded from file format " + std::to_string(result.fileSchemaVersion) + " to " +
                                  std::to_string(ProjectSchemaVersion::kCurrent) + ".");
    }

    Reader reader;
    ProjectDocument doc = readDocument(reader, root);
    if (!reader.errors.empty()) {
        result.errors = std::move(reader.errors);
        return result;
    }
    const auto report = validateProject(doc);
    for (const auto& issue : report.issues) {
        auto line = issue.path + ": " + issue.message;
        if (issue.severity == Severity::Error) result.errors.push_back(std::move(line));
        else result.warnings.push_back(std::move(line));
    }
    if (!report.ok()) return result;
    result.warnings.insert(result.warnings.end(), reader.warnings.begin(), reader.warnings.end());
    result.document = std::move(doc);
    return result;
}

std::optional<std::string> saveToFile(const ProjectDocument& doc, const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    const auto text = toJson(doc);
    std::random_device rd; // only for a unique temp name; never affects content
    const auto temp = path.parent_path() / (path.filename().string() + ".tmp-" + seedToString(rd()));
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return "Couldn't write to " + path.parent_path().string() + ".";
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(temp, ec);
            return "Couldn't finish writing the project. The previous file is unchanged.";
        }
    }
    std::error_code ec;
    fs::rename(temp, path, ec); // replaces the target atomically where the OS allows
    if (ec) {
        fs::remove(temp, ec);
        return "Couldn't replace " + path.filename().string() + ". The previous file is unchanged.";
    }
    return std::nullopt;
}

LoadResult loadFromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LoadResult r;
        r.errors.push_back("Couldn't open " + path.filename().string() + ".");
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return fromJson(ss.str());
}

} // namespace signalflux::persistence
