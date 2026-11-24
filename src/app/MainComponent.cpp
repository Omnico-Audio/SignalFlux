// SignalFlux — main window layout (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/MainComponent.h"

#include "domain/Examples.h"

namespace signalflux::app {

MainComponent::MainComponent() {
    viewport_.setViewedComponent(&canvas_, false);
    viewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(viewport_);
    addAndMakeVisible(inspector_);

    for (auto* b : {&newButton_, &openButton_, &saveButton_, &undoButton_, &redoButton_, &selectTool_, &stationTool_, &gateTool_,
                    &switchTool_, &playButton_, &stopButton_, &continueButton_, &panicButton_, &seedButton_, &captureButton_, &themeButton_})
        addAndMakeVisible(b);
    addAndMakeVisible(outputBox_);
    addAndMakeVisible(synthToggle_);
    addAndMakeVisible(statusLabel_);

    newButton_.onClick = [this] { state_.newProject(); };
    openButton_.onClick = [this] { openProject(); };
    saveButton_.onClick = [this] {
        if (state_.file() == juce::File()) saveProjectAs();
        else if (const auto err = state_.save(); err.isNotEmpty()) showMessage("Couldn't save", err);
    };
    undoButton_.onClick = [this] { state_.undo(); };
    redoButton_.onClick = [this] { state_.redo(); };

    const std::array<std::pair<juce::TextButton*, Tool>, 4> tools{{{&selectTool_, Tool::Select}, {&stationTool_, Tool::Station},
                                                                   {&gateTool_, Tool::Gate}, {&switchTool_, Tool::Switch}}};
    for (const auto& [button, tool] : tools) {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1);
        button->onClick = [this, t = tool] { canvas_.setTool(t); };
    }
    selectTool_.setToggleState(true, juce::dontSendNotification);
    selectTool_.setTooltip("Select and drag Stations. Shift-drag from one Station to another to connect them.");
    stationTool_.setTooltip("Click an empty cell to add a Station.");

    playButton_.onClick = [this] { host_.play(state_.seedForPlay()); };
    stopButton_.onClick = [this] { host_.stop(); };
    continueButton_.onClick = [this] { host_.continuePlayback(); };
    panicButton_.onClick = [this] { host_.panic(); };
    seedButton_.onClick = [this] { showSeedMenu(); };
    captureButton_.onClick = [this] { exportCapture(); };
    captureButton_.setTooltip("Save the last few minutes of output as a MIDI file.");
    themeButton_.onClick = [this] { state_.setDarkTheme(!state_.theme().dark); };

    outputBox_.onChange = [this] {
        const auto i = outputBox_.getSelectedItemIndex();
        const auto id = i <= 0 ? juce::String() : outputs_[i - 1].identifier;
        host_.selectOutput(id, i <= 0);
        state_.prefs().setValue("midiOutput", id);
    };
    refreshOutputs();

    if (const auto err = synth_.open(); err.isEmpty()) host_.setSynthQueue(&synth_.queue());
    else synthToggle_.setTooltip("No audio device: " + err + ". MIDI output is unaffected.");
    synthToggle_.setEnabled(synth_.available());
    synthToggle_.onClick = [this] { synth_.setEnabled(synthToggle_.getToggleState()); };

    for (int i = 0; i < 8; ++i) {
        auto& b = sceneButtons_[static_cast<std::size_t>(i)];
        b.setButtonText("Scene " + juce::String(i + 1));
        b.setTooltip("Click to recall. Shift-click to store the current mutes, Switches and tempo.");
        b.onClick = [this, i] { sceneClicked(i, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        addAndMakeVisible(b);
    }

    state_.addChangeListener(this);
    styleControls();
    setSize(1280, 800);
    setWantsKeyboardFocus(true);
    startTimerHz(15);

    // SF-010: offer crash recovery. Restoring never starts playback.
    if (state_.recoveryAvailable()) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Recover unsaved work?",
                                           "SignalFlux didn't close cleanly last time. Restore the autosaved project?",
                                           "Restore", "Discard", this,
                                           juce::ModalCallbackFunction::create([this](int ok) {
                                               if (ok != 0) state_.restoreRecovery();
                                               else state_.discardRecovery();
                                           }));
    } else if (auto example = examples::make("first-loop")) {
        state_.edit([&example](ProjectDocument& d) { d = *example; return true; }, "Open example");
    }
}

MainComponent::~MainComponent() {
    stopTimer();
    state_.removeChangeListener(this);
    host_.setSynthQueue(nullptr);
}

void MainComponent::styleControls() {
    const auto& th = state_.theme();
    auto& lf = getLookAndFeel();
    lf.setColour(juce::ResizableWindow::backgroundColourId, th.canvas);
    lf.setColour(juce::TextButton::buttonColourId, th.panel);
    lf.setColour(juce::TextButton::buttonOnColourId, th.panelBorder);
    lf.setColour(juce::TextButton::textColourOffId, th.text);
    lf.setColour(juce::TextButton::textColourOnId, th.text);
    lf.setColour(juce::ComboBox::backgroundColourId, th.panel);
    lf.setColour(juce::ComboBox::textColourId, th.text);
    lf.setColour(juce::ComboBox::outlineColourId, th.panelBorder);
    lf.setColour(juce::ToggleButton::textColourId, th.text);
    lf.setColour(juce::ToggleButton::tickColourId, th.text);
    lf.setColour(juce::Label::textColourId, th.textMuted);
    lf.setColour(juce::PropertyComponent::backgroundColourId, th.panel);
    lf.setColour(juce::PropertyComponent::labelTextColourId, th.text);
    lf.setColour(juce::TextEditor::backgroundColourId, th.canvas);
    lf.setColour(juce::TextEditor::textColourId, th.text);
    lf.setColour(juce::Slider::textBoxTextColourId, th.text);
    lf.setColour(juce::Slider::textBoxBackgroundColourId, th.canvas);
    lf.setColour(juce::ScrollBar::thumbColourId, th.panelBorder);
    themeButton_.setButtonText(th.dark ? "Light" : "Dark");
    sendLookAndFeelChange();
    repaint();
}

void MainComponent::refreshOutputs() {
    outputs_ = EngineHost::availableOutputs();
    outputBox_.clear(juce::dontSendNotification);
#if JUCE_WINDOWS
    outputBox_.addItem("MIDI out: none", 1);
#else
    outputBox_.addItem("MIDI out: SignalFlux virtual port", 1);
#endif
    const auto saved = state_.prefs().getValue("midiOutput");
    int select = 0;
    for (int i = 0; i < outputs_.size(); ++i) {
        outputBox_.addItem("MIDI out: " + outputs_[i].name, i + 2);
        if (outputs_[i].identifier == saved) select = i + 1;
    }
    outputBox_.setSelectedItemIndex(select, juce::sendNotification); // a missing device falls back without blocking (SF-010)
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    undoButton_.setEnabled(state_.canUndo());
    redoButton_.setEnabled(state_.canRedo());
    if (onTitleChanged) onTitleChanged(state_.title());
    for (int i = 0; i < 8; ++i) {
        bool stored = false;
        for (const auto& sc : state_.doc().scenes) stored |= sc.id.value() == static_cast<std::uint32_t>(i + 1);
        sceneButtons_[static_cast<std::size_t>(i)].setAlpha(stored ? 1.0f : 0.5f);
    }
    styleControls();
}

void MainComponent::timerCallback() {
    // MIDI Learn results and MIDI-triggered Scene recalls arrive from the engine thread.
    if (auto learned = host_.takeLearnedMapping())
        state_.edit([&learned](ProjectDocument& d) { editing::addMapping(d, *learned); return true; }, "MIDI Learn");
    if (const auto scene = host_.takeSceneRequest()) sceneClicked(static_cast<int>(scene->value()) - 1, false);

    const auto snap = host_.snapshot();
    juce::String s = snap.transport == engine::TransportState::Playing ? "Playing" : "Stopped";
    s << "  beat " << (snap.now.value() / kTicksPerQuarter + 1) << "  " << static_cast<int>(snap.travelers.size()) << " Travelers";
    s << "  " << juce::String(host_.tempo().value() / 1000.0, 1) << " BPM";
    if (snap.stats.droppedTravelers > 0) s << "  (" << static_cast<juce::int64>(snap.stats.droppedTravelers) << " over cap)";
    if (!host_.outputAvailable()) s << "  No MIDI output";
    statusLabel_.setText(s, juce::dontSendNotification);
}

void MainComponent::showSeedMenu() {
    juce::PopupMenu menu;
    const bool locked = state_.doc().settings.seedMode == SeedMode::Fixed;
    menu.addItem(1, locked ? "Unlock seed (new seed each Play)" : "Lock current seed", true, false);
    menu.addSeparator();
    menu.addSectionHeader("Recent seeds: click to recall, Shift-click to star");
    int id = 100;
    for (const auto& s : state_.seedHistory()) {
        const auto label = juce::String::toHexString(static_cast<juce::int64>(s.seed)) + "   " + s.when.toString(false, true) +
                           (state_.starred(s.seed) ? "   starred" : "");
        menu.addItem(id++, label);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(seedButton_), [this](int r) {
        if (r == 1) {
            state_.edit([](ProjectDocument& d) {
                d.settings.seedMode = d.settings.seedMode == SeedMode::Fixed ? SeedMode::NewEachPlay : SeedMode::Fixed;
                return true;
            }, "Seed lock");
        } else if (r >= 100) {
            const auto& h = state_.seedHistory();
            const auto i = static_cast<std::size_t>(r - 100);
            if (i >= h.size()) return;
            if (juce::ModifierKeys::currentModifiers.isShiftDown()) state_.toggleStar(h[i].seed);
            else state_.recallSeed(h[i].seed);
        }
    });
}

void MainComponent::openProject() {
    chooser_ = std::make_unique<juce::FileChooser>("Open project", juce::File(), "*.sflux");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (const auto err = state_.open(f); err.isNotEmpty()) showMessage("Couldn't open " + f.getFileName(), err);
    });
}

void MainComponent::saveProjectAs(std::function<void()> then) {
    chooser_ = std::make_unique<juce::FileChooser>("Save project", juce::File(), "*.sflux");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, then](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (const auto err = state_.saveAs(f); err.isNotEmpty()) showMessage("Couldn't save", err);
                              else if (then) then();
                          });
}

void MainComponent::exportCapture() {
    chooser_ = std::make_unique<juce::FileChooser>("Save capture as MIDI", juce::File(), "*.mid");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (const auto err = state_.exportCapture(f); err.isNotEmpty()) showMessage("Capture", err);
                          });
}

void MainComponent::sceneClicked(int slot, bool store) {
    if (slot < 0 || slot >= 8) return;
    const SceneId id{static_cast<std::uint32_t>(slot + 1)};
    if (store) {
        state_.edit([id, slot](ProjectDocument& d) {
            editing::storeScene(d, id, "Scene " + std::to_string(slot + 1));
            return true;
        }, "Store Scene");
        return;
    }
    bool exists = false;
    for (const auto& sc : state_.doc().scenes) exists |= sc.id == id;
    if (!exists) return;
    // Scene recall is a performance action (D27), applied on the beat (D35).
    auto next = editing::withSceneApplied(state_.doc(), id);
    host_.setDocumentQuantized(std::make_shared<const ProjectDocument>(next), next.settings.sceneQuantize);
    host_.setTempo(next.settings.tempo);
    state_.performanceChange(next);
}

void MainComponent::showMessage(const juce::String& title, const juce::String& text) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, text);
}

void MainComponent::requestQuit(std::function<void()> quit) {
    if (!state_.dirty()) return quit();
    juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::QuestionIcon, "Save changes?",
                                          "Save your changes before quitting?", "Save", "Don't save", "Cancel", this,
                                          juce::ModalCallbackFunction::create([this, quit](int r) {
                                              if (r == 2) return quit(); // don't save
                                              if (r != 1) return;        // cancel
                                              if (state_.file() == juce::File()) saveProjectAs(quit);
                                              else if (state_.save().isEmpty()) quit();
                                          }));
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress('z', cmd, 0)) return state_.undo(), true;
    if (key == juce::KeyPress('z', cmd | juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress('y', cmd, 0))
        return state_.redo(), true;
    if (key == juce::KeyPress('s', cmd, 0)) return saveButton_.triggerClick(), true;
    if (key == juce::KeyPress('o', cmd, 0)) return openProject(), true;
    if (key == juce::KeyPress('n', cmd, 0)) return state_.newProject(), true;
    if (key == juce::KeyPress::spaceKey) {
        if (host_.snapshot().transport == engine::TransportState::Playing) host_.stop();
        else host_.play(state_.seedForPlay());
        return true;
    }
    if (key == juce::KeyPress::escapeKey) return host_.panic(), true;
    return false;
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(state_.theme().panel); }

void MainComponent::resized() {
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(44).reduced(8, 6);
    auto place = [&bar](juce::Component& c, int w) {
        c.setBounds(bar.removeFromLeft(w));
        bar.removeFromLeft(4);
    };
    for (auto* b : {&newButton_, &openButton_, &saveButton_}) place(*b, 56);
    bar.removeFromLeft(8);
    place(undoButton_, 56);
    place(redoButton_, 56);
    bar.removeFromLeft(8);
    for (auto* b : {&selectTool_, &stationTool_, &gateTool_, &switchTool_}) place(*b, 64);
    bar.removeFromLeft(8);
    for (auto* b : {&playButton_, &stopButton_, &continueButton_, &panicButton_}) place(*b, 70);
    bar.removeFromLeft(8);
    place(seedButton_, 56);
    place(captureButton_, 70);
    place(outputBox_, 240);
    place(synthToggle_, 70);
    place(themeButton_, 56);

    auto scenes = r.removeFromBottom(40).reduced(8, 6);
    statusLabel_.setBounds(scenes.removeFromRight(420));
    for (auto& b : sceneButtons_) {
        b.setBounds(scenes.removeFromLeft(80));
        scenes.removeFromLeft(4);
    }
    inspector_.setBounds(r.removeFromRight(340));
    viewport_.setBounds(r);
}

} // namespace signalflux::app
