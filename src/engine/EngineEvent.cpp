// SignalFlux — canonical event formatting (D25).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/EngineEvent.h"

#include <charconv>
#include <string_view>

namespace signalflux::engine {

namespace {

std::string_view typeName(EngineEventType t) noexcept {
    switch (t) {
        case EngineEventType::TransportStart:    return "Start";
        case EngineEventType::TransportStop:     return "Stop";
        case EngineEventType::TransportContinue: return "Continue";
        case EngineEventType::Panic:             return "Panic";
        case EngineEventType::TravelerSpawn:     return "Spawn";
        case EngineEventType::TravelerArrive:    return "Arrive";
        case EngineEventType::TravelerDepart:    return "Depart";
        case EngineEventType::TravelerRepeat:    return "Repeat";
        case EngineEventType::TravelerEnd:       return "End";
        case EngineEventType::TravelerDropped:   return "TravelerDropped";
        case EngineEventType::NoteOn:            return "NoteOn";
        case EngineEventType::NoteOff:           return "NoteOff";
        case EngineEventType::NoteDropped:       return "NoteDropped";
        case EngineEventType::ControlChange:     return "CC";
        case EngineEventType::PitchBend:         return "PB";
    }
    return "Unknown";
}

std::string_view reasonName(EndReason r) noexcept {
    switch (r) {
        case EndReason::None:           return "none";
        case EndReason::NoRoute:        return "no-route";
        case EndReason::RouteDeleted:   return "route-deleted";
        case EndReason::StationDeleted: return "station-deleted";
        case EndReason::Terminated:     return "terminated";
    }
    return "unknown";
}

// Locale-independent integer formatting (D25: no std::to_string/iostreams locale effects).
void appendInt(std::string& out, std::int64_t v) {
    char buf[24];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
    (void)ec;
    out.append(buf, end);
}

void field(std::string& out, std::string_view key, std::int64_t v) {
    out += ' ';
    out += key;
    out += '=';
    appendInt(out, v);
}

} // namespace

std::string toCanonicalString(const EngineEvent& e) {
    std::string out;
    out.reserve(64);
    appendInt(out, e.tick.value());
    out += ' ';
    out += typeName(e.type);

    switch (e.type) {
        case EngineEventType::TransportStart:
        case EngineEventType::TransportStop:
        case EngineEventType::TransportContinue:
        case EngineEventType::Panic:
            break;
        case EngineEventType::TravelerSpawn:
        case EngineEventType::TravelerArrive:
        case EngineEventType::TravelerRepeat:
        case EngineEventType::TravelerDropped:
            field(out, "t", e.traveler.value());
            field(out, "st", e.station.value());
            break;
        case EngineEventType::TravelerDepart:
            field(out, "t", e.traveler.value());
            field(out, "st", e.station.value());
            field(out, "fp", e.flowpoint.value());
            field(out, "due", e.deadline.value());
            break;
        case EngineEventType::TravelerEnd:
            field(out, "t", e.traveler.value());
            field(out, "st", e.station.value());
            out += " why=";
            out += reasonName(e.endReason);
            break;
        case EngineEventType::NoteOn:
            field(out, "ch", e.channel.value());
            field(out, "n", e.note.value());
            field(out, "v", e.velocity.value());
            field(out, "st", e.station.value());
            field(out, "t", e.traveler.value());
            break;
        case EngineEventType::NoteOff:
            field(out, "ch", e.channel.value());
            field(out, "n", e.note.value());
            break;
        case EngineEventType::NoteDropped:
            field(out, "ch", e.channel.value());
            field(out, "n", e.note.value());
            field(out, "st", e.station.value());
            break;
        case EngineEventType::ControlChange:
            field(out, "ch", e.channel.value());
            field(out, "cc", e.controller.value());
            field(out, "val", e.value);
            break;
        case EngineEventType::PitchBend:
            field(out, "ch", e.channel.value());
            field(out, "val", e.value);
            break;
    }
    return out;
}

} // namespace signalflux::engine
