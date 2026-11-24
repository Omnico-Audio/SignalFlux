// SignalFlux — main window content (interim harness until SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/MainComponent.h"

#include "domain/Examples.h"

namespace signalflux::app {

namespace {
const juce::Colour kCanvas{0xff10131a};   // D40 Night transit
const juce::Colour kText{0xffe8eaf0};
} // namespace

MainComponent::MainComponent() {
    int id = 1;
    for (const auto name : examples::names()) exampleBox_.addItem(juce::String(std::string(name)), id++);
    exampleBox_.onChange = [this] { loadExample(exampleBox_.getSelectedItemIndex()); };
    outputBox_.onChange = [this] {
        const auto i = outputBox_.getSelectedItemIndex();
        if (i <= 0) host_.selectOutput({}, true);
        else host_.selectOutput(outputs_[i - 1].identifier, false);
    };
    playButton_.onClick = [this] { host_.play(static_cast<std::uint64_t>(seeds_.nextInt64())); };
    stopButton_.onClick = [this] { host_.stop(); };
    continueButton_.onClick = [this] { host_.continuePlayback(); };
    panicButton_.onClick = [this] { host_.panic(); };

    // Optional synth (SF-008). If no audio device opens, MIDI is unaffected.
    if (const auto err = synth_.open(); err.isEmpty()) host_.setSynthQueue(&synth_.queue());
    else synthToggle_.setButtonText("Monitor synth (unavailable: " + err + ")");
    synthToggle_.setEnabled(synth_.available());
    synthToggle_.onClick = [this] { synth_.setEnabled(synthToggle_.getToggleState()); };

    for (auto* c : std::initializer_list<juce::Component*>{&exampleBox_, &outputBox_, &playButton_, &stopButton_,
                                                           &continueButton_, &panicButton_, &synthToggle_})
        addAndMakeVisible(c);

    refreshOutputs();
    exampleBox_.setSelectedItemIndex(0);
    outputBox_.setSelectedItemIndex(0);
    setSize(960, 640);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    host_.setSynthQueue(nullptr);
}

void MainComponent::refreshOutputs() {
    outputs_ = EngineHost::availableOutputs();
    outputBox_.clear(juce::dontSendNotification);
#if JUCE_WINDOWS
    outputBox_.addItem("No output", 1);
#else
    outputBox_.addItem("SignalFlux virtual port", 1);
#endif
    for (int i = 0; i < outputs_.size(); ++i) outputBox_.addItem(outputs_[i].name, i + 2);
}

void MainComponent::loadExample(int index) {
    const auto names = examples::names();
    if (index < 0 || static_cast<std::size_t>(index) >= names.size()) return;
    if (auto doc = examples::make(names[static_cast<std::size_t>(index)]))
        host_.setDocument(std::make_shared<const ProjectDocument>(std::move(*doc)));
}

void MainComponent::timerCallback() {
    const auto snap = host_.snapshot();
    const char* transport = snap.transport == engine::TransportState::Playing ? "Playing"
                            : snap.transport == engine::TransportState::Paused ? "Stopped (Continue resumes)"
                                                                                : "Stopped";
    status_ = juce::String(transport) + "   beat " + juce::String(snap.now.value() / kTicksPerQuarter + 1) + "   " +
              juce::String(static_cast<int>(snap.travelers.size())) + " Travelers   " +
              (host_.outputAvailable() ? "MIDI out connected" : "No MIDI output");
    repaint();
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(32);
    area.removeFromTop(110);
    auto row = area.removeFromTop(32);
    exampleBox_.setBounds(row.removeFromLeft(200));
    row.removeFromLeft(12);
    outputBox_.setBounds(row.removeFromLeft(260));
    area.removeFromTop(12);
    row = area.removeFromTop(32);
    for (auto* b : {&playButton_, &stopButton_, &continueButton_, &panicButton_}) {
        b->setBounds(row.removeFromLeft(110));
        row.removeFromLeft(8);
    }
    area.removeFromTop(12);
    synthToggle_.setBounds(area.removeFromTop(28).removeFromLeft(420));
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(kCanvas);
    auto area = getLocalBounds().reduced(32);
    g.setColour(kText);
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText("SignalFlux", area.removeFromTop(48), juce::Justification::centredLeft);
    g.setColour(kText.withAlpha(0.6f));
    g.setFont(juce::FontOptions(16.0f));
    g.drawText(juce::String("Version ") + JUCE_APPLICATION_VERSION_STRING + "  (engine test harness; grid UI coming)",
               area.removeFromTop(28), juce::Justification::centredLeft);
    g.drawText(status_, area.removeFromTop(28), juce::Justification::centredLeft);
}

} // namespace signalflux::app
