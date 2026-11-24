// SignalFlux — main window content.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Interim test harness until the grid UI (SF-009): pick an example project and
// a MIDI output, then Play / Stop / Continue / Panic. Lets the MIDI services be
// checked on real hardware and in DAWs.
//
// Thread ownership: message thread only.

#pragma once

#include "app/EngineHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

class MainComponent final : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadExample(int index);
    void refreshOutputs();

    EngineHost host_;
    juce::ComboBox exampleBox_, outputBox_;
    juce::TextButton playButton_{"Play"}, stopButton_{"Stop"}, continueButton_{"Continue"}, panicButton_{"Panic"};
    juce::Array<juce::MidiDeviceInfo> outputs_;
    juce::String status_;
    juce::Random seeds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace signalflux::app
