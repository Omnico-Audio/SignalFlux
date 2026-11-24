// SignalFlux — main window content (placeholder until SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Thread ownership: message thread only.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

class MainComponent final : public juce::Component {
public:
    MainComponent();
    void paint(juce::Graphics& g) override;

private:
    juce::String statusLine_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace signalflux::app
