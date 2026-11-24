// SignalFlux — application entry point.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace signalflux::app {

class MainWindow final : public juce::DocumentWindow {
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(0xff15171c), DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        auto* content = new MainComponent();
        content->onTitleChanged = [this](const juce::String& t) { setName(t); };
        setContentOwned(content, true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    void requestQuit(std::function<void()> quit) {
        if (auto* c = dynamic_cast<MainComponent*>(getContentComponent())) c->requestQuit(std::move(quit));
        else quit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class SignalFluxApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { window_ = std::make_unique<MainWindow>(getApplicationName()); }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override {
        if (window_) window_->requestQuit([] { juce::JUCEApplication::getInstance()->quit(); });
        else quit();
    }

private:
    std::unique_ptr<MainWindow> window_;
};

} // namespace signalflux::app

START_JUCE_APPLICATION(signalflux::app::SignalFluxApplication)
