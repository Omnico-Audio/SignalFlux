// SignalFlux — the Plot canvas (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Draws Stations, routes (orthogonal, coloured by source Station, with a
// direction marker and beat ticks, D31), gates, Switches and Travelers.
// Travelers are projections of engine snapshots: their position is
// interpolated from (legStart, deadline) and the snapshot's tick plus the wall
// time since it arrived. Dropped frames only make sprites jump; musical timing
// is never touched (SF-009 acceptance).
//
// Editing sends model commands through AppState; nothing here mutates engine
// state. Thread ownership: message thread.

#pragma once

#include "app/AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

enum class Tool : std::uint8_t { Select, Station, Gate, Switch };

class GridCanvas final : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit GridCanvas(AppState& state);
    ~GridCanvas() override;

    void setTool(Tool t) { tool_ = t; }
    [[nodiscard]] Tool tool() const noexcept { return tool_; }
    static constexpr int kCell = 32;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { updateSize(); repaint(); }
    void timerCallback() override;
    void updateSize();

    [[nodiscard]] juce::Point<float> centre(GridCoordinate c) const;
    [[nodiscard]] GridCoordinate cellAt(juce::Point<int> p) const;
    [[nodiscard]] std::optional<FlowpointId> routeNear(juce::Point<float> p) const;
    [[nodiscard]] const Gate* gateAt(GridCoordinate c) const;
    [[nodiscard]] const Switch* switchAt(GridCoordinate c) const;
    void deleteSelection();

    void drawRoutes(juce::Graphics& g);
    void drawStations(juce::Graphics& g);
    void drawLogic(juce::Graphics& g);
    void drawTravelers(juce::Graphics& g);

    AppState& state_;
    Tool tool_ = Tool::Select;
    engine::EngineSnapshot snap_;
    double snapTimeMs_ = 0.0;
    std::optional<StationId> dragging_;
    std::optional<StationId> connectingFrom_;
    juce::Point<int> dragPoint_;
    int dragGesture_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridCanvas)
};

} // namespace signalflux::app
