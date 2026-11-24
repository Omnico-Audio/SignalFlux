// SignalFlux — MIDI input routing and MIDI Learn.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "midi/MidiInputRouter.h"

#include <algorithm>

namespace signalflux::midi {

std::int32_t transposeFromCc(int value, std::int32_t range) noexcept {
    // 128 values spread evenly over 2R+1 steps; 64 (centre) maps to 0 for even spreads.
    const auto steps = 2 * range + 1;
    return static_cast<std::int32_t>(std::clamp(value, 0, 127) * steps / 128) - range;
}

MilliBpm tempoFromCc(int value) noexcept {
    const auto v = std::clamp(value, 0, 127);
    return *MilliBpm::make(MilliBpm::kMin + static_cast<std::int64_t>(v) * (MilliBpm::kMax - MilliBpm::kMin) / 127);
}

std::int32_t transposeFromPitchBend(int value, std::int32_t range) noexcept {
    const auto v = std::clamp(value, -8192, 8191);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(v) * range / (v < 0 ? 8192 : 8191));
}

bool MidiInputRouter::matches(const MidiSource& src, MidiSourceType type, int channel, int number,
                              const std::string& device) const {
    if (src.type != type) return false;
    if (src.deviceName && *src.deviceName != device) return false;
    if (src.channel && src.channel->value() != channel) return false;
    return type == MidiSourceType::PitchBend || src.number == number;
}

void MidiInputRouter::apply(const MidiMapping& m, MidiSourceType type, int value, IMappingTarget& target) {
    switch (m.target) {
        case MappingTarget::GlobalTranspose:
            if (type == MidiSourceType::ControlChange) target.transpose(transposeFromCc(value, m.rangeSteps));
            else if (type == MidiSourceType::PitchBend) target.transpose(transposeFromPitchBend(value, m.rangeSteps));
            else target.transpose(std::clamp(value - 60, -m.rangeSteps, m.rangeSteps)); // keyboard: 60 = no shift
            return;
        case MappingTarget::Tempo:
            target.tempo(tempoFromCc(type == MidiSourceType::PitchBend ? (value + 8192) / 129 : value));
            return;
        case MappingTarget::StationVelocity:
            if (m.station) target.stationVelocity(*m.station, *Velocity::make(std::clamp(value, 1, 127)));
            return;
        case MappingTarget::StationProbability:
            if (m.station) target.stationProbability(*m.station, *ProbabilityPercent::make(std::clamp(value, 0, 127) * 100 / 127));
            return;
        case MappingTarget::SceneRecall:
            if (m.scene) target.sceneRecall(*m.scene);
            return;
    }
}

std::optional<MidiMapping> MidiInputRouter::handle(const std::uint8_t* bytes, std::size_t size, const std::string& device,
                                                   IMappingTarget& target) {
    if (size < 2 || bytes[0] < 0x80 || bytes[0] >= 0xF0) return std::nullopt; // system and running-status bytes: ignored
    const int kind = bytes[0] & 0xF0;
    const int channel = (bytes[0] & 0x0F) + 1;
    const auto ch = *MidiChannel::make(channel);
    const int d1 = bytes[1] & 0x7F;
    const int d2 = size >= 3 ? bytes[2] & 0x7F : 0;

    MidiSourceType type{};
    int number = 0;
    int value = 0;
    bool noteOff = false;
    switch (kind) {
        case 0x90:
        case 0x80:
            type = MidiSourceType::Note;
            number = d1;
            noteOff = kind == 0x80 || d2 == 0;
            value = noteOff ? 0 : d2;
            target.gateNote(ch, *MidiNote::make(d1), !noteOff);
            break;
        case 0xB0:
            type = MidiSourceType::ControlChange;
            number = d1;
            value = d2;
            if (d1 <= CcNumber::kMax) target.gateControl(ch, *CcNumber::make(d1), *CcValue::make(d2));
            break;
        case 0xE0:
            type = MidiSourceType::PitchBend;
            value = ((d2 << 7) | d1) - 8192;
            break;
        default:
            return std::nullopt; // Program Change, aftertouch: not supported in v1
    }

    if (learn_) {
        if (type == MidiSourceType::Note && noteOff) return std::nullopt; // learn from the press, not the release
        MidiMapping m;
        m.source = MidiSource{device, ch, type, number};
        m.target = learn_->target;
        m.station = learn_->station;
        m.scene = learn_->scene;
        learn_.reset();
        return m;
    }

    for (std::size_t i = 0; i < mappings_.size(); ++i) {
        const auto& m = mappings_[i];
        if (!matches(m.source, type, channel, number, device)) continue;
        if (m.target == MappingTarget::SceneRecall) {
            // Fire on the rising edge only: a note press, or a CC crossing 64 upward.
            const bool on = type == MidiSourceType::Note ? !noteOff : value >= 64;
            const bool fire = on && sceneLatch_[i] == 0;
            sceneLatch_[i] = on ? 1 : 0;
            if (!fire) continue;
        } else if (type == MidiSourceType::Note && noteOff && m.target != MappingTarget::GlobalTranspose) {
            continue; // releases don't change velocity, probability or tempo
        } else if (type == MidiSourceType::Note && noteOff) {
            continue; // keyboard transpose holds its last value on release
        }
        apply(m, type, value, target);
    }
    return std::nullopt;
}

} // namespace signalflux::midi
