// SignalFlux — property inspector for the selection (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Shows the selected Station, route, gate or Switch, or the project settings
// when nothing is selected. Every change is an AppState edit (undoable, D27);
// repeated changes to one property merge into a single undo step.
//
// Thread ownership: message thread.

#pragma once

#include "app/AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

class Inspector final : public juce::Component, private juce::ChangeListener {
public:
    explicit Inspector(AppState& state);
    ~Inspector() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();
    void buildStation(StationId id);
    void buildFlowpoint(FlowpointId id);
    void buildGate(GateId id);
    void buildSwitch(SwitchId id);
    void buildProject();

    AppState& state_;
    juce::Label heading_;
    juce::PropertyPanel panel_;
    Selection shown_;
    std::size_t shownSteps_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Inspector)
};

} // namespace signalflux::app
