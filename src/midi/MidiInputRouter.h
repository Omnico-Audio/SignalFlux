// SignalFlux — MIDI input routing and MIDI Learn (task SF-007; DECISIONS D15, D30).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Turns raw incoming MIDI into engine actions:
//  - every Note On/Off and CC also feeds gate inputs (D12);
//  - messages matching a mapping drive its target (transpose, tempo, Station
//    velocity or probability, Scene recall);
//  - in Learn mode, the next Note On, CC or Pitch Bend becomes a mapping
//    source for the armed target instead.
// Supported: Note On/Off, Control Change, Pitch Bend. Everything else
// (SysEx, Program Change, clock, transport, aftertouch) is ignored in v1.
//
// Thread ownership: engine thread (messages arrive through an SPSC queue).

#pragma once

#include "domain/Project.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace signalflux::midi {

/// What mappings can change. Implemented by the engine host.
class IMappingTarget {
public:
    virtual ~IMappingTarget() = default;
    virtual void gateNote(MidiChannel ch, MidiNote note, bool held) = 0;
    virtual void gateControl(MidiChannel ch, CcNumber cc, CcValue value) = 0;
    virtual void transpose(std::int32_t scaleSteps) = 0;
    virtual void tempo(MilliBpm tempo) = 0;
    virtual void stationVelocity(StationId station, Velocity v) = 0;
    virtual void stationProbability(StationId station, ProbabilityPercent p) = 0;
    virtual void sceneRecall(SceneId scene) = 0;
};

struct LearnRequest {
    MappingTarget target = MappingTarget::GlobalTranspose;
    std::optional<StationId> station;
    std::optional<SceneId> scene;
};

class MidiInputRouter {
public:
    /// Off the deadline path (allocates): call when the document changes.
    void setMappings(std::vector<MidiMapping> mappings) {
        mappings_ = std::move(mappings);
        sceneLatch_.assign(mappings_.size(), 0);
    }

    /// Arms MIDI Learn: the next usable message becomes the source for this target.
    void armLearn(LearnRequest request) { learn_ = request; }
    void cancelLearn() noexcept { learn_.reset(); }
    [[nodiscard]] bool learning() const noexcept { return learn_.has_value(); }

    /// Handles one complete MIDI message from `device`. Returns a new mapping
    /// when Learn captured this message (the caller adds it to the document
    /// as an undoable edit, replacing any mapping with the same source, D30).
    std::optional<MidiMapping> handle(const std::uint8_t* bytes, std::size_t size, const std::string& device,
                                      IMappingTarget& target);

private:
    bool matches(const MidiSource& src, MidiSourceType type, int channel, int number, const std::string& device) const;
    void apply(const MidiMapping& m, MidiSourceType type, int value, IMappingTarget& target);

    std::vector<MidiMapping> mappings_;
    std::optional<LearnRequest> learn_;
    std::vector<std::uint8_t> sceneLatch_; // rising-edge detection for CC scene triggers, per mapping
};

/// Value conversions used by mappings (exposed for tests and UI readouts).
[[nodiscard]] std::int32_t transposeFromCc(int value, std::int32_t range) noexcept;     // 0–127 -> −R…+R
[[nodiscard]] MilliBpm tempoFromCc(int value) noexcept;                                  // 0–127 -> 20–300 BPM
[[nodiscard]] std::int32_t transposeFromPitchBend(int value, std::int32_t range) noexcept; // −8192…8191 -> −R…+R

} // namespace signalflux::midi
