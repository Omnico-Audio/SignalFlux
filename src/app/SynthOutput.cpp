// SignalFlux — audio device bridge for the optional synth.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/SynthOutput.h"

namespace signalflux::app {

SynthOutput::SynthOutput() = default;

SynthOutput::~SynthOutput() { devices_.removeAudioCallback(this); }

juce::String SynthOutput::open() {
    const auto error = devices_.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty() || devices_.getCurrentAudioDevice() == nullptr) {
        available_.store(false);
        return error.isNotEmpty() ? error : juce::String("No audio output device");
    }
    devices_.addAudioCallback(this);
    available_.store(true);
    return {};
}

void SynthOutput::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    synth_.prepare(device->getCurrentSampleRate()); // may allocate: not the audio callback
}

void SynthOutput::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&) {
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr) juce::FloatVectorOperations::clear(outputs[c], numSamples);

    const bool on = enabled_.load(std::memory_order_relaxed);
    if (on != wasEnabled_) {
        if (!on) synth_.allNotesOff();
        wasEnabled_ = on;
    }

    // Drain everything queued since the last block (bounded by the queue size).
    while (auto e = queue_.pop()) {
        if (!on) continue;
        switch (e->kind) {
            case SynthEvent::Kind::NoteOn: synth_.noteOn(e->channel, e->pitch, e->velocity); break;
            case SynthEvent::Kind::NoteOff: synth_.noteOff(e->channel, e->pitch); break;
            case SynthEvent::Kind::AllOff: synth_.allNotesOff(); break;
        }
    }
    if (numOutputs >= 2 && outputs[0] != nullptr && outputs[1] != nullptr)
        synth_.render(outputs[0], outputs[1], numSamples);
    else if (numOutputs == 1 && outputs[0] != nullptr)
        synth_.render(outputs[0], outputs[0], numSamples);
}

} // namespace signalflux::app
