// SignalFlux — document lifecycle, selection, preferences (tasks SF-009, SF-010).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Owns the current ProjectDocument and its undo history (D27). Every edit is
// applied to a copy, validated, committed, and sent to the engine as an
// immutable snapshot; the UI never touches engine-owned runtime state.
//
// Also: open/save with atomic writes (D17), autosave and crash recovery that
// never starts playback (SF-010), seed history (D29), theme preference (D40),
// and the Capture buffer (D28).
//
// Thread ownership: message thread only.

#pragma once

#include "app/EngineHost.h"
#include "app/Theme.h"
#include "domain/Editing.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <deque>
#include <functional>

namespace signalflux::app {

enum class SelectionKind : std::uint8_t { None, Station, Flowpoint, Gate, Switch };

struct Selection {
    SelectionKind kind = SelectionKind::None;
    std::uint32_t id = 0;
    friend bool operator==(const Selection&, const Selection&) = default;
};

struct SeedEntry {
    std::uint64_t seed = 0;
    juce::String project;
    juce::Time when;
};

class AppState final : public juce::ChangeBroadcaster, private juce::Timer {
public:
    explicit AppState(EngineHost& host);
    ~AppState() override;

    [[nodiscard]] const ProjectDocument& doc() const { return history_.current(); }
    [[nodiscard]] EngineHost& host() noexcept { return host_; }

    /// Applies `change` to a copy of the document. If it returns true and the
    /// result is valid, the edit becomes an undo step (merged with the previous
    /// one when `gesture` matches) and the engine receives the new document.
    bool edit(const std::function<bool(ProjectDocument&)>& change, const std::string& label,
              const std::string& gesture = {});
    void endGesture() { history_.endGesture(); }
    /// Performance changes (Switch flips, Scene recall) are saved with the
    /// project but are not undo steps (D27).
    void performanceChange(const ProjectDocument& doc);

    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return history_.canRedo(); }

    // Files (D17)
    void newProject();
    juce::String open(const juce::File& file); // empty string on success
    juce::String save();                       // to the current file; empty string on success
    juce::String saveAs(const juce::File& file);
    [[nodiscard]] const juce::File& file() const { return file_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] juce::String title() const;

    /// SF-010: a recovery file newer than the last clean exit, if any.
    [[nodiscard]] std::optional<juce::File> recoveryAvailable() const;
    juce::String restoreRecovery();
    void discardRecovery();

    // Selection
    void select(Selection s);
    [[nodiscard]] Selection selection() const noexcept { return selection_; }

    // Theme (D40)
    [[nodiscard]] const Theme& theme() const { return darkTheme_ ? nightTransit() : paperSketch(); }
    void setDarkTheme(bool dark);

    // Seeds (D29)
    /// Seed for the next Play: the project's seed when locked (Fixed), else a fresh one.
    std::uint64_t seedForPlay();
    [[nodiscard]] const std::deque<SeedEntry>& seedHistory() const { return seeds_; }
    void recallSeed(std::uint64_t seed);      // locks the project to this seed
    void toggleStar(std::uint64_t seed);
    [[nodiscard]] bool starred(std::uint64_t seed) const;

    // Capture (D28)
    void pumpEngineEvents();                  // call every UI frame
    juce::String exportCapture(const juce::File& file);
    [[nodiscard]] std::size_t capturedEvents() const { return capture_.size(); }

    [[nodiscard]] juce::PropertiesFile& prefs() { return *props_.getUserSettings(); }

private:
    void timerCallback() override; // autosave
    void documentChanged(bool sendToEngine = true);
    [[nodiscard]] juce::File recoveryFile() const;

    EngineHost& host_;
    editing::History history_;
    juce::File file_;
    bool dirty_ = false;
    bool dirtySinceAutosave_ = false;
    Selection selection_;
    bool darkTheme_ = true;
    std::deque<SeedEntry> seeds_;
    std::uint64_t lastSeed_ = 0;
    std::deque<engine::EngineEvent> capture_;
    std::vector<engine::EngineEvent> drainScratch_;
    mutable juce::ApplicationProperties props_;
    juce::Random random_;
};

} // namespace signalflux::app
