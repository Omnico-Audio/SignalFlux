// SignalFlux — audio device bridge for the optional synth (task SF-008).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// The engine thread pushes note events into a bounded SPSC queue; the audio
// callback drains it at the start of each block and renders. Events apply at
// block start, so monitoring jitter is at most one audio buffer; musical timing
// (and MIDI output) is decided by the engine and is unaffected by sample rate
// or buffer size (ACCEPTANCE §4).
//
// If no audio device can be opened, the synth is simply unavailable; MIDI keeps
// working (ARCHITECTURE §10).

#pragma once

#include "audio/Synth.h"
#include "midi/SpscQueue.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>

namespace signalflux::app {

struct SynthEvent {
    enum class Kind : std::uint8_t { NoteOn, NoteOff, AllOff } kind = Kind::AllOff;
    std::uint8_t channel = 1, pitch = 0, velocity = 0;
};

using SynthQueue = midi::SpscQueue<SynthEvent, 2048>;

class SynthOutput final : private juce::AudioIODeviceCallback {
public:
    SynthOutput();
    ~SynthOutput() override;

    /// Opens the default output device. Returns an empty string on success,
    /// otherwise a message for the UI. Message thread.
    juce::String open();

    void setEnabled(bool on) noexcept { enabled_.store(on); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }
    [[nodiscard]] bool available() const noexcept { return available_.load(); }

    /// The engine thread's producer end. Push is lock-free; when full, events are dropped.
    [[nodiscard]] SynthQueue& queue() noexcept { return queue_; }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager devices_;
    audio::Synth synth_;
    SynthQueue queue_;
    std::atomic<bool> enabled_{false}, available_{false};
    bool wasEnabled_ = false; // audio thread only
};

} // namespace signalflux::app
