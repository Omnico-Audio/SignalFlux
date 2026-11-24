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
// Interaction:
//  - Select tool: click to select, drag a Station to move it, double-click an
//    empty cell to add a Station, Shift/Alt-drag between Stations to connect.
//  - Station / Connect / Gate / Switch tools: click to place or to connect.
//  - Right-click anything (or empty space) for a context menu.
//  - Cmd/Ctrl + mouse wheel, or + / -, to zoom.
// Editing sends model commands through AppState; nothing here mutates engine
// state. Thread ownership: message thread.

#pragma once

#include "app/AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

enum class Tool : std::uint8_t { Select, Station, Connect, Gate, Switch };

class GridCanvas final : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit GridCanvas(AppState& state);
    ~GridCanvas() override;

    void setTool(Tool t);
    [[nodiscard]] Tool tool() const noexcept { return tool_; }
    /// Called when the tool changes from inside the canvas (e.g. a context menu).
    std::function<void(Tool)> onToolChanged;
    /// One-line hint for the current tool or gesture, for the status area.
    [[nodiscard]] juce::String hint() const;
    std::function<void()> onHintChanged;

    void zoom(int steps);
    [[nodiscard]] juce::Point<int> centreOfContent() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { updateSize(); repaint(); }
    void timerCallback() override;
    void updateSize();

    [[nodiscard]] juce::Point<float> centre(GridCoordinate c) const;
    [[nodiscard]] juce::Point<float> toScreen(double cellX, double cellY) const;
    [[nodiscard]] std::optional<GridCoordinate> cellAt(juce::Point<int> p) const;
    [[nodiscard]] std::optional<FlowpointId> routeNear(juce::Point<float> p) const;
    /// Screen polyline of a route. A route with a partner in the opposite
    /// direction is drawn offset to its own right, so both stay visible.
    struct ScreenRoute {
        std::array<juce::Point<float>, 3> points;
        float length = 0.0f;
        [[nodiscard]] juce::Point<float> along(double fraction) const;
    };
    [[nodiscard]] std::optional<ScreenRoute> screenRoute(const Flowpoint& f) const;
    [[nodiscard]] const Gate* gateAt(GridCoordinate c) const;
    [[nodiscard]] const Switch* switchAt(GridCoordinate c) const;
    void deleteSelection();
    void addStationAt(GridCoordinate cell);
    void connect(StationId from, StationId to);
    void flipSwitch(SwitchId id);
    void showContextMenu(const juce::MouseEvent& e);

    void drawRoutes(juce::Graphics& g);
    void drawStations(juce::Graphics& g);
    void drawLogic(juce::Graphics& g);
    void drawTravelers(juce::Graphics& g);

    AppState& state_;
    Tool tool_ = Tool::Select;
    int cell_ = 40;                              // pixels per grid cell (zoom)
    static constexpr int kMargin = 24;           // keeps edge Stations off the border
    engine::EngineSnapshot snap_;
    double snapTimeMs_ = 0.0;
    std::optional<StationId> dragging_;
    std::optional<StationId> connectingFrom_;    // drag-to-connect in progress, or Connect tool's first click
    bool connectByClicks_ = false;
    juce::Point<int> dragPoint_;
    std::optional<GridCoordinate> hoverCell_;
    int dragGesture_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridCanvas)
};

} // namespace signalflux::app
