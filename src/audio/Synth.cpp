// SignalFlux — optional 16-voice monitoring synth.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "audio/Synth.h"

#include <cmath>

namespace signalflux::audio {

void Synth::prepare(double sampleRate) noexcept {
    for (int n = 0; n < 128; ++n) {
        const double hz = 440.0 * std::pow(2.0, (n - 69) / 12.0);
        increments_[static_cast<std::size_t>(n)] = static_cast<float>(hz / sampleRate);
    }
    const auto perSample = [sampleRate](double seconds) { return static_cast<float>(1.0 / (seconds * sampleRate)); };
    attackStep_ = perSample(0.005);
    decayStep_ = perSample(0.12);
    releaseStep_ = perSample(0.25);
    voices_ = {};
}

int Synth::pickVoice() const noexcept {
    int best = -1;
    for (int i = 0; i < kVoices; ++i)
        if (voices_[static_cast<std::size_t>(i)].stage == Stage::Idle) return i;
    for (int i = 0; i < kVoices; ++i) { // oldest voice already releasing
        const auto& v = voices_[static_cast<std::size_t>(i)];
        if (v.stage == Stage::Release && (best < 0 || v.releasedAt < voices_[static_cast<std::size_t>(best)].releasedAt))
            best = i;
    }
    if (best >= 0) return best;
    best = 0;
    for (int i = 1; i < kVoices; ++i) // oldest held voice
        if (voices_[static_cast<std::size_t>(i)].order < voices_[static_cast<std::size_t>(best)].order) best = i;
    return best;
}

void Synth::noteOn(int channel, int pitch, int velocity) noexcept {
    if (pitch < 0 || pitch > 127 || velocity < 1) return;
    auto& v = voices_[static_cast<std::size_t>(pickVoice())];
    v.stage = Stage::Attack;
    v.channel = channel;
    v.pitch = pitch;
    v.gain = 0.25f * static_cast<float>(velocity) / 127.0f;
    v.level = 0.0f;
    v.phase = 0.0f;
    v.increment = increments_[static_cast<std::size_t>(pitch)];
    v.pan = 0.3f + 0.4f * static_cast<float>((channel - 1) % 16) / 15.0f; // spread channels gently across the stereo field
    v.order = ++counter_;
}

void Synth::noteOff(int channel, int pitch) noexcept {
    for (auto& v : voices_)
        if (v.stage != Stage::Idle && v.stage != Stage::Release && v.channel == channel && v.pitch == pitch) {
            v.stage = Stage::Release;
            v.releasedAt = ++counter_;
        }
}

void Synth::allNotesOff() noexcept {
    for (auto& v : voices_)
        if (v.stage != Stage::Idle && v.stage != Stage::Release) {
            v.stage = Stage::Release;
            v.releasedAt = ++counter_;
        }
}

int Synth::activeVoices() const noexcept {
    int n = 0;
    for (const auto& v : voices_) n += v.stage != Stage::Idle ? 1 : 0;
    return n;
}

void Synth::render(float* left, float* right, int numSamples) noexcept {
    for (auto& v : voices_) {
        if (v.stage == Stage::Idle) continue;
        for (int i = 0; i < numSamples; ++i) {
            switch (v.stage) {
                case Stage::Attack:
                    v.level += attackStep_;
                    if (v.level >= 1.0f) {
                        v.level = 1.0f;
                        v.stage = Stage::Decay;
                    }
                    break;
                case Stage::Decay:
                    v.level -= decayStep_ * (1.0f - sustain_);
                    if (v.level <= sustain_) {
                        v.level = sustain_;
                        v.stage = Stage::Sustain;
                    }
                    break;
                case Stage::Sustain: break;
                case Stage::Release:
                    v.level -= releaseStep_ * sustain_;
                    if (v.level <= 0.0f) {
                        v.level = 0.0f;
                        v.stage = Stage::Idle;
                    }
                    break;
                case Stage::Idle: break;
            }
            if (v.stage == Stage::Idle) break;
            // Soft triangle: cheap, few harmonics, fine for monitoring.
            const float tri = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f;
            v.phase += v.increment;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            const float s = tri * v.level * v.gain;
            left[i] += s * (1.0f - v.pan);
            right[i] += s * v.pan;
        }
    }
}

} // namespace signalflux::audio
