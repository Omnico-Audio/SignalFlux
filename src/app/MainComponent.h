// SignalFlux — main window layout (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
//  toolbar: file, undo/redo, tools, transport, seed, Capture, MIDI out, synth, theme
//  grid canvas (scrollable)  |  inspector
//  Scenes bar (8 slots: click recalls, Shift-click stores)  |  status
//
// Thread ownership: message thread.

#pragma once

#include "app/AppState.h"
#include "app/GridCanvas.h"
#include "app/Inspector.h"
#include "app/SynthOutput.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// Window title follows the file name and unsaved state.
    std::function<void(const juce::String&)> onTitleChanged;
    /// Asks before quitting with unsaved changes. Calls `quit` if it is OK to quit.
    void requestQuit(std::function<void()> quit);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void styleControls();
    void refreshOutputs();
    void showSeedMenu();
    void openProject();
    void saveProjectAs(std::function<void()> then = {});
    void exportCapture();
    void sceneClicked(int slot, bool store);
    void showMessage(const juce::String& title, const juce::String& text);

    SynthOutput synth_;
    EngineHost host_;
    AppState state_{host_};
    GridCanvas canvas_{state_};
    juce::Viewport viewport_;
    Inspector inspector_{state_};

    juce::TextButton newButton_{"New"}, openButton_{"Open"}, saveButton_{"Save"}, undoButton_{"Undo"}, redoButton_{"Redo"};
    juce::TextButton selectTool_{"Select"}, stationTool_{"Station"}, gateTool_{"Gate"}, switchTool_{"Switch"};
    juce::TextButton playButton_{"Play"}, stopButton_{"Stop"}, continueButton_{"Continue"}, panicButton_{"Panic"};
    juce::TextButton seedButton_{"Seed"}, captureButton_{"Capture"}, themeButton_{"Light"};
    juce::ComboBox outputBox_;
    juce::ToggleButton synthToggle_{"Synth"};
    juce::Label statusLabel_;
    std::array<juce::TextButton, 8> sceneButtons_;
    juce::Array<juce::MidiDeviceInfo> outputs_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace signalflux::app
