// SignalFlux — recording event sink and offline render.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/EventLog.h"

#include "engine/Sequencer.h"

namespace signalflux::engine {

void EventLog::onEngineEvent(const EngineEvent& event) noexcept {
    try {
        events_.push_back(event);
    } catch (...) {
        // Out of memory in a test/offline sink: drop the event rather than terminate.
    }
}

std::string EventLog::canonical() const {
    std::string out;
    out.reserve(events_.size() * 40);
    for (const auto& e : events_) {
        out += toCanonicalString(e);
        out += '\n';
    }
    return out;
}

std::string renderCanonicalLog(const ProjectDocument& doc, MusicalTick length) {
    EventLog log;
    Sequencer seq{log};
    if (!seq.setDocument(doc).ok()) return {};
    seq.play();
    seq.processUntil(length);
    seq.requestStop();
    if (seq.pendingStop()) seq.processUntil(*seq.pendingStop());
    return log.canonical();
}

} // namespace signalflux::engine
