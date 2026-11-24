// SignalFlux — real-time engine host (task SF-007; ARCHITECTURE §6–9; DECISIONS D14, D16, D28, D37).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Owns the Sequencer and runs it on a dedicated 1 ms high-resolution timer
// thread (the "engine thread"). Musical time comes from an integer
// PlaybackClock over a monotonic clock; the UI frame rate plays no part.
//
// Threads and ownership:
//  - Message thread (UI) calls the public methods. They only push commands into
//    a bounded SPSC queue and never block on the engine.
//  - Engine thread owns the Sequencer, the open MIDI output, the input router
//    and the clock. It sends MIDI directly (sendMessageNow), so worst-case
//    lateness is one timer period (about 1 ms).
//  - JUCE's MIDI input thread pushes raw messages into a second SPSC queue.
//  - The engine thread publishes snapshots and engine events for the UI, the
//    synth (SF-008) and Capture (D28) through try-locked slots and bounded queues,
//    so the engine never waits for a reader.
//
// Overflow policies: when the command queue is full the UI call returns false;
// when the MIDI input queue is full the message is dropped and counted.

#pragma once

#include "engine/Sequencer.h"
#include "midi/MidiInputRouter.h"
#include "midi/PlaybackClock.h"
#include "midi/SpscQueue.h"

#include <juce_audio_devices/juce_audio_devices.h>

#if SIGNALFLUX_WITH_LINK
#include <ableton/Link.hpp>
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace signalflux::app {

/// One emitted engine event with its tick, for the synth and Capture.
struct TimedEvent {
    engine::EngineEvent event;
};

class EngineHost final : private juce::HighResolutionTimer,
                         private juce::MidiInputCallback,
                         private engine::IEngineEventSink,
                         private midi::IMappingTarget {
public:
    EngineHost();
    ~EngineHost() override;

    // --- Message thread API. Each returns false only if the command queue is full.
    bool setDocument(std::shared_ptr<const ProjectDocument> doc);
    /// Applies a document at the next beat (or 4 beats) boundary: Scene recall (D35).
    bool setDocumentQuantized(std::shared_ptr<const ProjectDocument> doc, SceneQuantize quantize);
    bool play(std::uint64_t seed);
    bool stop();
    bool continuePlayback();
    bool panic();
    bool setSwitch(SwitchId id, bool on);
    bool setTempo(MilliBpm tempo);
    bool armLearn(midi::LearnRequest request);
    bool cancelLearn();

    /// Opens the output device with this identifier (empty: none). On macOS
    /// and Linux, `virtualPort` also creates a "SignalFlux" virtual output (D16).
    void selectOutput(const juce::String& identifier, bool virtualPort);
    /// Opens these input devices (by identifier); others are closed.
    void selectInputs(const juce::StringArray& identifiers);

    /// Latest engine state for drawing. Copies under a short lock.
    [[nodiscard]] engine::EngineSnapshot snapshot() const;
    /// Mapping captured by MIDI Learn since the last call, if any (D30).
    [[nodiscard]] std::optional<MidiMapping> takeLearnedMapping();
    /// Scene requested by a MIDI mapping since the last call, if any (D35).
    [[nodiscard]] std::optional<SceneId> takeSceneRequest();
    /// Moves engine events emitted since the last call into `out` (UI-side
    /// Capture buffer, synth bridge). Bounded: if the UI falls behind, the oldest
    /// undelivered events are dropped and counted.
    void drainEvents(std::vector<engine::EngineEvent>& out);

    [[nodiscard]] bool outputAvailable() const noexcept { return outputAvailable_.load(); }
    [[nodiscard]] std::uint64_t droppedInputMessages() const noexcept { return droppedInput_.load(); }
    [[nodiscard]] std::uint64_t droppedEvents() const noexcept { return droppedEvents_.load(); }
    [[nodiscard]] MilliBpm tempo() const noexcept { return MilliBpm::make(tempoMilli_.load()).value_or(MilliBpm{}); }

    /// Device lists for the settings UI.
    static juce::Array<juce::MidiDeviceInfo> availableOutputs() { return juce::MidiOutput::getAvailableDevices(); }
    static juce::Array<juce::MidiDeviceInfo> availableInputs() { return juce::MidiInput::getAvailableDevices(); }

private:
    enum class CommandKind : std::uint8_t {
        None, Document, DocumentQuantized, Play, Stop, Continue, Panic, Switch, Tempo, ArmLearn, CancelLearn,
        Output, OutputLost, Inputs
    };
    struct Command {
        CommandKind kind = CommandKind::None;
        std::shared_ptr<const ProjectDocument> doc;
        std::shared_ptr<juce::MidiOutput> output;
        std::shared_ptr<std::vector<juce::String>> inputNames;
        std::uint64_t seed = 0;
        SwitchId sw;
        bool on = false;
        MilliBpm tempo;
        SceneQuantize quantize = SceneQuantize::Immediate;
        midi::LearnRequest learn;
    };
    struct RawMidi {
        std::array<std::uint8_t, 3> bytes{};
        std::uint8_t size = 0;
        std::uint8_t device = 0;
    };

    bool push(Command c);
    void hiResTimerCallback() override;                     // engine thread
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& m) override; // MIDI thread
    void onEngineEvent(const engine::EngineEvent& e) noexcept override;                         // engine thread
    void apply(Command& c, std::int64_t nowUs);
    void send(const std::uint8_t* bytes, std::size_t size) noexcept;
    void advance(std::int64_t nowUs);
    void publish();
    void checkDevices();                                    // message thread

    // IMappingTarget (engine thread)
    void gateNote(MidiChannel ch, MidiNote note, bool held) override { seq_.setMidiNoteHeld(ch, note, held); }
    void gateControl(MidiChannel ch, CcNumber cc, CcValue v) override { seq_.setMidiControl(ch, cc, v); }
    void transpose(std::int32_t steps) override { seq_.setTranspose(steps); }
    void tempo(MilliBpm t) override;
    void stationVelocity(StationId s, Velocity v) override { seq_.setVelocityOverride(s, v); }
    void stationProbability(StationId s, ProbabilityPercent p) override { seq_.setProbabilityOverride(s, p); }
    void sceneRecall(SceneId s) override { sceneRequest_.store(s.value()); }

    static std::int64_t nowMicros() noexcept;

    // Engine-thread state
    engine::Sequencer seq_{*this};
    midi::MidiInputRouter router_;
    midi::PlaybackClock clock_;
    std::shared_ptr<juce::MidiOutput> output_;
    std::shared_ptr<const ProjectDocument> doc_;
    std::shared_ptr<const ProjectDocument> pendingDoc_;
    MusicalTick pendingAt_;
    std::vector<juce::String> inputNames_;
    MusicalTick nextPulse_;
    std::int64_t nextFreePulseUs_ = 0;
    bool sendClock_ = true, clockWhileStopped_ = false, sppBeforeStart_ = true;
    bool deferring_ = false;                          // buffering tick-0 events during Play (D14 order)
    std::vector<engine::EngineEvent> deferred_;
    std::optional<MidiMapping> learnedPending_;       // waits here if the UI holds the lock
    engine::EngineSnapshot scratch_;
    int publishCountdown_ = 0;

    // Cross-thread
    midi::SpscQueue<Command, 256> commands_;
    midi::SpscQueue<RawMidi, 1024> midiIn_;
    midi::SpscQueue<engine::EngineEvent, 8192> events_;
    mutable std::mutex snapshotMutex_;
    engine::EngineSnapshot published_;
    std::mutex learnedMutex_;
    std::optional<MidiMapping> learned_;
    std::atomic<std::uint32_t> sceneRequest_{0};
    std::atomic<bool> outputAvailable_{false};
    std::atomic<std::uint64_t> droppedInput_{0}, droppedEvents_{0};
    std::atomic<std::int32_t> tempoMilli_{MilliBpm::kDefault};

    // Message-thread state
    juce::String outputId_;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs_;
    juce::MidiDeviceListConnection deviceWatcher_;

#if SIGNALFLUX_WITH_LINK
    // D37: network tempo sync. enable() is called on the message thread; the
    // engine thread uses the real-time-safe audio session state calls.
    ableton::Link link_{120.0};
    std::int32_t linkTempoSent_ = 0;
    void syncLink() noexcept;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineHost)
};

} // namespace signalflux::app
