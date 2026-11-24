// SignalFlux — project validation (task SF-002).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Validation.h"

#include "domain/Geometry.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <variant>

namespace signalflux {

bool ValidationReport::ok() const noexcept { return errorCount() == 0; }

bool ValidationReport::has(IssueCode code) const noexcept {
    return std::any_of(issues.begin(), issues.end(), [code](const ValidationIssue& i) { return i.code == code; });
}

std::size_t ValidationReport::errorCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == Severity::Error; }));
}

std::size_t ValidationReport::warningCount() const noexcept { return issues.size() - errorCount(); }

std::string_view toString(IssueCode code) noexcept {
    switch (code) {
        case IssueCode::UnsupportedSchemaVersion:          return "UnsupportedSchemaVersion";
        case IssueCode::PlotCountNotOne:                   return "PlotCountNotOne";
        case IssueCode::TooManyScenes:                     return "TooManyScenes";
        case IssueCode::TooManyOutputs:                    return "TooManyOutputs";
        case IssueCode::InvalidId:                         return "InvalidId";
        case IssueCode::DuplicateId:                       return "DuplicateId";
        case IssueCode::PlotSizeOutOfRange:                return "PlotSizeOutOfRange";
        case IssueCode::StationOutOfBounds:                return "StationOutOfBounds";
        case IssueCode::StationCellOccupied:               return "StationCellOccupied";
        case IssueCode::FlowpointUnknownStation:           return "FlowpointUnknownStation";
        case IssueCode::FlowpointSelfLoop:                 return "FlowpointSelfLoop";
        case IssueCode::FlowpointWaypointsUnsupported:     return "FlowpointWaypointsUnsupported";
        case IssueCode::OutgoingOrderMismatch:             return "OutgoingOrderMismatch";
        case IssueCode::RouteRateInexact:                  return "RouteRateInexact";
        case IssueCode::ValueOutOfRange:                   return "ValueOutOfRange";
        case IssueCode::ChordInvalid:                      return "ChordInvalid";
        case IssueCode::GateArity:                         return "GateArity";
        case IssueCode::GateUnknownReference:              return "GateUnknownReference";
        case IssueCode::StationMultipleDrivers:            return "StationMultipleDrivers";
        case IssueCode::SceneUnknownReference:             return "SceneUnknownReference";
        case IssueCode::MappingMissingTarget:              return "MappingMissingTarget";
        case IssueCode::MappingUnknownReference:           return "MappingUnknownReference";
        case IssueCode::NoStartStation:                    return "NoStartStation";
        case IssueCode::AllRandomWeightsZero:              return "AllRandomWeightsZero";
        case IssueCode::PassOverridesPassThrough:          return "PassOverridesPassThrough";
        case IssueCode::PassThroughIgnoredOutsideRelative: return "PassThroughIgnoredOutsideRelative";
        case IssueCode::ForceScaleIgnoredOutsideAbsolute:  return "ForceScaleIgnoredOutsideAbsolute";
    }
    return "Unknown";
}

namespace {

std::string indexed(const std::string& base, std::string_view field, std::size_t i) {
    return base + (base.empty() ? "" : ".") + std::string(field) + "[" + std::to_string(i) + "]";
}

std::string member(const std::string& base, std::string_view field) {
    return base + "." + std::string(field);
}

class Validator {
public:
    explicit Validator(const ProjectDocument& doc) : doc_(doc) {}

    ValidationReport run() {
        collectGlobalIds();
        checkDocument();
        checkSettings();
        for (std::size_t p = 0; p < doc_.plots.size(); ++p) checkPlot(doc_.plots[p], indexed("", "plots", p));
        checkScenes();
        checkMidi();
        return std::move(report_);
    }

private:
    void error(IssueCode code, std::string path, std::string message) {
        report_.issues.push_back({Severity::Error, code, std::move(path), std::move(message)});
    }
    void warning(IssueCode code, std::string path, std::string message) {
        report_.issues.push_back({Severity::Warning, code, std::move(path), std::move(message)});
    }

    template <typename T>
    void range(const std::string& path, T value, T lo, T hi) {
        if (value < lo || value > hi)
            error(IssueCode::ValueOutOfRange, path,
                  "value " + std::to_string(value) + " outside " + std::to_string(lo) + "…" + std::to_string(hi));
    }

    /// Checks validity and uniqueness of one ID within its kind.
    template <typename Id>
    void checkId(Id id, std::set<std::uint32_t>& seen, const std::string& path) {
        if (!id.isValid()) {
            error(IssueCode::InvalidId, path, "ID 0 is reserved");
            return;
        }
        if (!seen.insert(id.value()).second)
            error(IssueCode::DuplicateId, path, "duplicate ID " + std::to_string(id.value()));
    }

    // References may point anywhere in the document, so gather all IDs first.
    void collectGlobalIds() {
        for (const auto& plot : doc_.plots) {
            for (const auto& s : plot.stations) allStations_.insert(s.id.value());
            for (const auto& g : plot.gates) allGates_.insert(g.id.value());
            for (const auto& sw : plot.switches) allSwitches_.insert(sw.id.value());
        }
        for (const auto& sc : doc_.scenes) allScenes_.insert(sc.id.value());
    }

    void checkDocument() {
        if (doc_.schemaVersion.value != ProjectSchemaVersion::kCurrent)
            error(IssueCode::UnsupportedSchemaVersion, "schemaVersion",
                  "in-memory documents must be at schema " + std::to_string(ProjectSchemaVersion::kCurrent));
        if (doc_.plots.size() != 1)
            error(IssueCode::PlotCountNotOne, "plots",
                  "v1 projects contain exactly one Plot; found " + std::to_string(doc_.plots.size()));
        if (doc_.scenes.size() > limits::kMaxScenes)
            error(IssueCode::TooManyScenes, "scenes", "at most 8 scenes");
        if (doc_.midi.outputs.size() > limits::kMaxOutputs)
            error(IssueCode::TooManyOutputs, "midi.outputs", "v1 supports one MIDI output device");
    }

    void checkSettings() {
        const auto& s = doc_.settings;
        range("settings.swingPercent", s.swingPercent, limits::kMinSwing, limits::kMaxSwing);
        range("settings.travelerCap", s.travelerCap, limits::kMinTravelerCap, limits::kMaxTravelerCap);
        range("settings.activeNoteCap", s.activeNoteCap, limits::kMinActiveNoteCap, limits::kMaxActiveNoteCap);
        range("settings.captureMinutes", s.captureMinutes, limits::kMinCaptureMinutes, limits::kMaxCaptureMinutes);
    }

    void checkPlot(const Plot& plot, const std::string& path) {
        checkId(plot.id, plotIds_, member(path, "id"));
        if (plot.width < limits::kMinPlotSize || plot.width > limits::kMaxPlotSize ||
            plot.height < limits::kMinPlotSize || plot.height > limits::kMaxPlotSize)
            error(IssueCode::PlotSizeOutOfRange, path, "Plot size must be 1–1024 cells per side");

        std::set<std::uint32_t> plotStations;
        std::map<GridCoordinate, std::size_t> occupied;
        bool anyStart = false;

        for (std::size_t i = 0; i < plot.stations.size(); ++i) {
            const auto& st = plot.stations[i];
            const auto sp = indexed(path, "stations", i);
            checkId(st.id, stationIds_, member(sp, "id"));
            plotStations.insert(st.id.value());
            anyStart = anyStart || st.isStart;

            const auto pos = st.position;
            if (pos.x < 0 || pos.y < 0 || pos.x >= plot.width || pos.y >= plot.height)
                error(IssueCode::StationOutOfBounds, member(sp, "position"), "Station lies outside the Plot");
            if (const auto [it, inserted] = occupied.emplace(pos, i); !inserted)
                error(IssueCode::StationCellOccupied, member(sp, "position"),
                      "cell already holds stations[" + std::to_string(it->second) + "]");

            checkStationParameters(st, sp);
        }

        if (!plot.stations.empty() && !anyStart)
            warning(IssueCode::NoStartStation, member(path, "stations"), "no Start Station: Play will produce nothing");

        // Flowpoints
        std::map<std::uint32_t, std::vector<std::uint32_t>> outgoingByStation; // station -> flowpoint ids in document order
        std::map<std::uint32_t, const Flowpoint*> flowpointById;
        for (std::size_t i = 0; i < plot.flowpoints.size(); ++i) {
            const auto& fp = plot.flowpoints[i];
            const auto fpp = indexed(path, "flowpoints", i);
            checkId(fp.id, flowpointIds_, member(fpp, "id"));
            flowpointById.emplace(fp.id.value(), &fp);

            const bool fromOk = plotStations.count(fp.from.value()) != 0;
            const bool toOk = plotStations.count(fp.to.value()) != 0;
            if (!fromOk) error(IssueCode::FlowpointUnknownStation, member(fpp, "from"), "unknown Station");
            if (!toOk) error(IssueCode::FlowpointUnknownStation, member(fpp, "to"), "unknown Station");
            if (fp.from == fp.to)
                error(IssueCode::FlowpointSelfLoop, fpp, "a Flowpoint cannot connect a Station to itself; use Repeat");
            if (!fp.waypoints.empty())
                error(IssueCode::FlowpointWaypointsUnsupported, member(fpp, "waypoints"), "waypoints are reserved for a later schema");
            if (!isRateExact(doc_.settings.gridSpacing, fp.rate))
                error(IssueCode::RouteRateInexact, member(fpp, "rate"),
                      "rate " + std::to_string(fp.rate.numerator()) + "/" + std::to_string(fp.rate.denominator()) +
                      " does not give whole-tick durations at the current grid spacing");
            if (fromOk) outgoingByStation[fp.from.value()].push_back(fp.id.value());
        }

        // Outgoing order must list exactly this Station's outgoing Flowpoints, once each (D6).
        for (std::size_t i = 0; i < plot.stations.size(); ++i) {
            const auto& st = plot.stations[i];
            const auto sp = indexed(path, "stations", i);
            std::vector<std::uint32_t> expected = outgoingByStation[st.id.value()];
            std::vector<std::uint32_t> listed;
            for (const auto id : st.outgoingOrder) listed.push_back(id.value());
            std::sort(expected.begin(), expected.end());
            std::sort(listed.begin(), listed.end());
            if (expected != listed)
                error(IssueCode::OutgoingOrderMismatch, member(sp, "outgoingOrder"),
                      "must list each outgoing Flowpoint of this Station exactly once");

            if (st.branchMode == BranchMode::Random && !st.outgoingOrder.empty()) {
                bool anyWeight = false;
                for (const auto id : st.outgoingOrder)
                    if (const auto it = flowpointById.find(id.value()); it != flowpointById.end() && it->second->weight.value() > 0)
                        anyWeight = true;
                if (!anyWeight)
                    warning(IssueCode::AllRandomWeightsZero, member(sp, "outgoingOrder"),
                            "every route weight is 0: Travelers end at this Station (D32)");
            }
        }

        // Switches
        for (std::size_t i = 0; i < plot.switches.size(); ++i)
            checkId(plot.switches[i].id, switchIds_, member(indexed(path, "switches", i), "id"));

        // Gates
        std::map<std::uint32_t, std::size_t> driverCount;
        for (std::size_t i = 0; i < plot.gates.size(); ++i) {
            const auto& g = plot.gates[i];
            const auto gp = indexed(path, "gates", i);
            checkId(g.id, gateIds_, member(gp, "id"));

            const auto n = g.inputs.size();
            if (g.type == GateType::Not ? n != 1 : (n < 1 || n > limits::kMaxGateInputs))
                error(IssueCode::GateArity, member(gp, "inputs"),
                      g.type == GateType::Not ? "NOT takes exactly one input" : "gates take 1–16 inputs");

            for (std::size_t k = 0; k < g.inputs.size(); ++k) {
                const auto ip = indexed(gp, "inputs", k);
                std::visit([&](const auto& in) { checkGateInput(in, ip); }, g.inputs[k]);
            }

            std::set<std::uint32_t> targetsOfThisGate;
            for (std::size_t k = 0; k < g.targets.size(); ++k) {
                const auto t = g.targets[k].value();
                if (allStations_.count(t) == 0)
                    error(IssueCode::GateUnknownReference, indexed(gp, "targets", k), "unknown Station");
                else if (targetsOfThisGate.insert(t).second)
                    ++driverCount[t];
            }
        }
        for (const auto& [station, count] : driverCount)
            if (count > 1)
                error(IssueCode::StationMultipleDrivers, member(path, "gates"),
                      "Station " + std::to_string(station) + " is driven by " + std::to_string(count) +
                      " gates; combine them with another gate (D12)");
    }

    void checkGateInput(const StationActiveInput& in, const std::string& path) {
        if (allStations_.count(in.station.value()) == 0) error(IssueCode::GateUnknownReference, path, "unknown Station");
    }
    void checkGateInput(const GateOutputInput& in, const std::string& path) {
        // Cycles, including self-feedback, are allowed and have defined semantics (D12).
        if (allGates_.count(in.gate.value()) == 0) error(IssueCode::GateUnknownReference, path, "unknown gate");
    }
    void checkGateInput(const SwitchInput& in, const std::string& path) {
        if (allSwitches_.count(in.sw.value()) == 0) error(IssueCode::GateUnknownReference, path, "unknown Switch");
    }
    void checkGateInput(const MidiNoteInput&, const std::string&) {}
    void checkGateInput(const MidiCcInput&, const std::string&) {}

    void checkStationParameters(const Station& st, const std::string& sp) {
        const auto& n = st.note;
        const auto np = member(sp, "note");
        range(member(np, "duration"), n.duration.value(), limits::kMinNoteDuration, limits::kMaxNoteDuration);
        range(member(np, "relativeSteps"), n.relativeSteps, -limits::kMaxRelativeSteps, limits::kMaxRelativeSteps);
        range(member(np, "random.rangeSteps"), n.random.rangeSteps, 0, limits::kMaxRandomRange);
        range(member(np, "random.boundSteps"), n.random.boundSteps, 0, limits::kMaxWalkBound);

        if (!n.chordOffsets.empty()) {
            std::set<std::int32_t> unique(n.chordOffsets.begin(), n.chordOffsets.end());
            const bool inRange = std::all_of(n.chordOffsets.begin(), n.chordOffsets.end(),
                [](std::int32_t o) { return o >= -limits::kMaxChordOffset && o <= limits::kMaxChordOffset; });
            if (n.chordOffsets.size() > limits::kMaxChordTones || unique.size() != n.chordOffsets.size() ||
                unique.count(0) == 0 || !inRange)
                error(IssueCode::ChordInvalid, member(np, "chordOffsets"),
                      "a chord has 1–6 unique offsets in −24…24 including 0");
        }

        if (n.relativePassThrough && n.mode != NoteMode::Relative)
            warning(IssueCode::PassThroughIgnoredOutsideRelative, member(np, "relativePassThrough"),
                    "Relative Pass-through only applies in Relative mode");
        if (n.forceScale && n.mode != NoteMode::Absolute)
            warning(IssueCode::ForceScaleIgnoredOutsideAbsolute, member(np, "forceScale"),
                    "Force Scale only applies in Absolute mode (D11)");
        if (n.pass && n.relativePassThrough)
            warning(IssueCode::PassOverridesPassThrough, member(np, "pass"),
                    "Pass overrides Relative Pass-through");

        range(member(sp, "additionalRepeats"), st.additionalRepeats, 0u, limits::kMaxAdditionalRepeats);
        range(member(sp, "colour"), static_cast<int>(st.colour), 0, limits::kPaletteSize - 1);
        if (st.repeatInterval)
            range(member(sp, "repeatInterval"), st.repeatInterval->value(), limits::kMinNoteDuration, limits::kMaxNoteDuration);
        range(member(sp, "humanize.velocityRange"), st.humanize.velocityRange, 0, limits::kMaxHumanizeVelocity);
        range(member(sp, "humanize.maxDelay"), st.humanize.maxDelay.value(), std::int64_t{0}, limits::kMaxHumanizeDelay);
    }

    void checkScenes() {
        for (std::size_t i = 0; i < doc_.scenes.size(); ++i) {
            const auto& sc = doc_.scenes[i];
            const auto sp = indexed("", "scenes", i);
            checkId(sc.id, sceneIds_, member(sp, "id"));
            for (std::size_t k = 0; k < sc.mutedStations.size(); ++k)
                if (allStations_.count(sc.mutedStations[k].value()) == 0)
                    error(IssueCode::SceneUnknownReference, indexed(sp, "mutedStations", k), "unknown Station");
            for (std::size_t k = 0; k < sc.switchStates.size(); ++k)
                if (allSwitches_.count(sc.switchStates[k].sw.value()) == 0)
                    error(IssueCode::SceneUnknownReference, indexed(sp, "switchStates", k), "unknown Switch");
        }
    }

    void checkMidi() {
        std::set<std::uint32_t> mappingIds;
        for (std::size_t i = 0; i < doc_.midi.mappings.size(); ++i) {
            const auto& m = doc_.midi.mappings[i];
            const auto mp = indexed("midi", "mappings", i);
            checkId(m.id, mappingIds, member(mp, "id"));
            range(member(mp, "source.number"), m.source.number, 0, 127);
            range(member(mp, "rangeSteps"), m.rangeSteps, 1, limits::kMaxRelativeSteps);

            const bool needsStation = m.target == MappingTarget::StationVelocity || m.target == MappingTarget::StationProbability;
            const bool needsScene = m.target == MappingTarget::SceneRecall;
            if (needsStation && !m.station)
                error(IssueCode::MappingMissingTarget, member(mp, "station"), "this mapping target needs a Station");
            if (needsScene && !m.scene)
                error(IssueCode::MappingMissingTarget, member(mp, "scene"), "this mapping target needs a Scene");
            if (needsStation && m.station && allStations_.count(m.station->value()) == 0)
                error(IssueCode::MappingUnknownReference, member(mp, "station"), "unknown Station");
            if (needsScene && m.scene && allScenes_.count(m.scene->value()) == 0)
                error(IssueCode::MappingUnknownReference, member(mp, "scene"), "unknown Scene");
        }
    }

    const ProjectDocument& doc_;
    ValidationReport report_;
    std::set<std::uint32_t> allStations_, allGates_, allSwitches_, allScenes_;
    std::set<std::uint32_t> plotIds_, stationIds_, flowpointIds_, gateIds_, switchIds_, sceneIds_;
};

} // namespace

ValidationReport validateProject(const ProjectDocument& doc) { return Validator{doc}.run(); }

} // namespace signalflux
