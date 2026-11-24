// SignalFlux — optional 16-voice monitoring synth (task SF-008; ARCHITECTURE §10).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Consumes the same note events as MIDI output; it never gates, delays or
// changes them. JUCE-free so it can be tested headless.
//
// Real-time rules: prepare() may allocate; noteOn/noteOff/allNotesOff/render
// never allocate, never lock, and do bounded work (16 voices × block size).
//
// Voice stealing (deterministic): a Note On takes a free voice if there is one;
// otherwise the oldest voice already in release; otherwise the oldest voice.
// "Oldest" is by Note On order, so the same event sequence always plays the
// same voices. A Note Off releases every voice holding that (channel, pitch),
// matching the engine's reference-counted Note Off (D22).

#pragma once

#include <array>
#include <cstdint>

namespace signalflux::audio {

class Synth {
public:
    static constexpr int kVoices = 16;

    void prepare(double sampleRate) noexcept;

    void noteOn(int channel, int pitch, int velocity) noexcept; // channel 1–16, pitch 0–127, velocity 1–127
    void noteOff(int channel, int pitch) noexcept;
    void allNotesOff() noexcept;                                // release everything (Stop, Panic)

    /// Adds `numSamples` of audio into `left` and `right`.
    void render(float* left, float* right, int numSamples) noexcept;

    // Introspection for tests and meters.
    [[nodiscard]] int activeVoices() const noexcept;
    [[nodiscard]] int voicePitch(int voice) const noexcept { return voices_[static_cast<std::size_t>(voice)].pitch; }
    [[nodiscard]] bool voiceHeld(int voice) const noexcept {
        return voices_[static_cast<std::size_t>(voice)].stage == Stage::Attack ||
               voices_[static_cast<std::size_t>(voice)].stage == Stage::Decay ||
               voices_[static_cast<std::size_t>(voice)].stage == Stage::Sustain;
    }

private:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };
    struct Voice {
        Stage stage = Stage::Idle;
        int channel = 0, pitch = -1;
        float gain = 0.0f, level = 0.0f, phase = 0.0f, increment = 0.0f, pan = 0.5f;
        std::uint64_t order = 0;      // Note On sequence number
        std::uint64_t releasedAt = 0; // release sequence number
    };

    int pickVoice() const noexcept;

    std::array<Voice, kVoices> voices_{};
    std::array<float, 128> increments_{};
    float attackStep_ = 0.0f, decayStep_ = 0.0f, releaseStep_ = 0.0f;
    float sustain_ = 0.6f;
    std::uint64_t counter_ = 0;
};

} // namespace signalflux::audio
