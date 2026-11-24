// SignalFlux — main window content (placeholder until SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/MainComponent.h"

#include "domain/Validation.h"

namespace signalflux::app {

MainComponent::MainComponent() {
    const auto report = validateProject(makeDefaultProject());
    statusLine_ = juce::String("Domain model: default project ") + (report.ok() ? "valid" : "INVALID");
    statusLine_ << "   |   Ableton Link: " << (SIGNALFLUX_WITH_LINK ? "built in" : "not built");
    setSize(960, 640);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15171c));
    auto area = getLocalBounds().reduced(32);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText("SignalFlux", area.removeFromTop(48), juce::Justification::centredLeft);

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(16.0f));
    g.drawText(juce::String("Version ") + JUCE_APPLICATION_VERSION_STRING + "  (pre-beta scaffold)",
               area.removeFromTop(28), juce::Justification::centredLeft);
    g.drawText(statusLine_, area.removeFromTop(28), juce::Justification::centredLeft);
}

} // namespace signalflux::app
