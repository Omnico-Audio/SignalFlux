// SignalFlux — real-time engine host (task SF-007).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/EngineHost.h"

#include "midi/MidiBytes.h"

#include <chrono>
#include <cstring>

namespace signalflux::app {

namespace {
constexpr int kTimerPeriodMs = 1;
constexpr int kPublishEveryMs = 16; // ~60 snapshots per second for the UI
} // namespace

std::int64_t EngineHost::nowMicros() noexcept {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

EngineHost::EngineHost() {
    deferred_.reserve(1024);
    deviceWatcher_ = juce::MidiDeviceListConnection::make([this] { checkDevices(); });
    startTimer(kTimerPeriodMs);
}

EngineHost::~EngineHost() {
    stopTimer(); // waits for the engine thread callback to finish
    for (auto& in : inputs_) in->stop();
    if (output_) {
        // Never leave notes hanging when the app quits (PRODUCT §8).
        seq_.panic();
    }
}

bool EngineHost::push(Command c) { return commands_.push(std::move(c)); }

bool EngineHost::setDocument(std::shared_ptr<const ProjectDocument> doc) {
#if SIGNALFLUX_WITH_LINK
    if (doc) link_.enable(doc->settings.linkEnabled); // not real-time safe: message thread only
#endif
    Command c;
    c.kind = CommandKind::Document;
    c.doc = std::move(doc);
    return push(std::move(c));
}

bool EngineHost::setDocumentQuantized(std::shared_ptr<const ProjectDocument> doc, SceneQuantize quantize) {
    Command c;
    c.kind = CommandKind::DocumentQuantized;
    c.doc = std::move(doc);
    c.quantize = quantize;
    return push(std::move(c));
}

bool EngineHost::play(std::uint64_t seed) {
    Command c;
    c.kind = CommandKind::Play;
    c.seed = seed;
    return push(std::move(c));
}

bool EngineHost::stop() { Command c; c.kind = CommandKind::Stop; return push(std::move(c)); }
bool EngineHost::continuePlayback() { Command c; c.kind = CommandKind::Continue; return push(std::move(c)); }
bool EngineHost::panic() { Command c; c.kind = CommandKind::Panic; return push(std::move(c)); }
bool EngineHost::cancelLearn() { Command c; c.kind = CommandKind::CancelLearn; return push(std::move(c)); }

bool EngineHost::setSwitch(SwitchId id, bool on) {
    Command c;
    c.kind = CommandKind::Switch;
    c.sw = id;
    c.on = on;
    return push(std::move(c));
}

bool EngineHost::setTempo(MilliBpm t) {
    Command c;
    c.kind = CommandKind::Tempo;
    c.tempo = t;
    return push(std::move(c));
}

bool EngineHost::armLearn(midi::LearnRequest request) {
    Command c;
    c.kind = CommandKind::ArmLearn;
    c.learn = request;
    return push(std::move(c));
}

void EngineHost::selectOutput(const juce::String& identifier, bool virtualPort) {
    outputId_ = identifier;
    std::shared_ptr<juce::MidiOutput> out;
    if (identifier.isNotEmpty()) out = juce::MidiOutput::openDevice(identifier);
#if !JUCE_WINDOWS
    else if (virtualPort) out = juce::MidiOutput::createNewDevice("SignalFlux"); // D16; Windows: use a loopback driver
#else
    juce::ignoreUnused(virtualPort);
#endif
    Command c;
    c.kind = CommandKind::Output;
    c.output = std::move(out);
    push(std::move(c));
}

void EngineHost::selectInputs(const juce::StringArray& identifiers) {
    for (auto& in : inputs_) in->stop();
    inputs_.clear();
    auto names = std::make_shared<std::vector<juce::String>>();
    for (const auto& info : juce::MidiInput::getAvailableDevices()) {
        if (!identifiers.contains(info.identifier) || names->size() >= 255) continue;
        if (auto in = juce::MidiInput::openDevice(info.identifier, this)) {
            names->push_back(info.name);
            in->start();
            inputs_.push_back(std::move(in));
        }
    }
    Command c;
    c.kind = CommandKind::Inputs;
    c.inputNames = std::move(names);
    push(std::move(c));
}

void EngineHost::checkDevices() {
    if (outputId_.isEmpty()) return;
    for (const auto& info : juce::MidiOutput::getAvailableDevices())
        if (info.identifier == outputId_) return;
    Command c; // D16: device gone. Engine keeps running; nothing is sent until reconnect.
    c.kind = CommandKind::OutputLost;
    push(std::move(c));
}

engine::EngineSnapshot EngineHost::snapshot() const {
    std::lock_guard lock(snapshotMutex_);
    return published_;
}

std::optional<MidiMapping> EngineHost::takeLearnedMapping() {
    std::lock_guard lock(learnedMutex_);
    auto out = std::move(learned_);
    learned_.reset();
    return out;
}

std::optional<SceneId> EngineHost::takeSceneRequest() {
    const auto v = sceneRequest_.exchange(0);
    if (v == 0) return std::nullopt;
    return SceneId{v};
}

void EngineHost::drainEvents(std::vector<engine::EngineEvent>& out) {
    while (auto e = events_.pop()) out.push_back(*e);
}

// ---------------------------------------------------------------------------
// MIDI input thread
// ---------------------------------------------------------------------------

void EngineHost::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& m) {
    const auto size = m.getRawDataSize();
    if (size < 1 || size > 3) return; // SysEx and other long messages are not supported in v1 (D15)
    RawMidi raw;
    std::memcpy(raw.bytes.data(), m.getRawData(), static_cast<std::size_t>(size));
    raw.size = static_cast<std::uint8_t>(size);
    for (std::size_t i = 0; i < inputs_.size(); ++i)
        if (inputs_[i].get() == source) raw.device = static_cast<std::uint8_t>(i);
    if (!midiIn_.push(raw)) droppedInput_.fetch_add(1);
}

// ---------------------------------------------------------------------------
// Engine thread
// ---------------------------------------------------------------------------

void EngineHost::tempo(MilliBpm t) {
    clock_.setTempo(t, nowMicros());
    tempoMilli_.store(t.value());
}

void EngineHost::send(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (!output_) return;
    output_->sendMessageNow(juce::MidiMessage(bytes, static_cast<int>(size)));
}

void EngineHost::onEngineEvent(const engine::EngineEvent& e) noexcept {
    if (deferring_ && e.type != engine::EngineEventType::TransportStart) {
        if (deferred_.size() < deferred_.capacity()) deferred_.push_back(e);
        return;
    }
    std::array<midi::MidiMessage, midi::kMaxMessagesPerEvent> msgs{};
    const auto n = midi::encode(e, sppBeforeStart_, msgs);
    for (std::size_t i = 0; i < n; ++i) send(msgs[i].bytes.data(), msgs[i].size);
    if (!events_.push(e)) droppedEvents_.fetch_add(1);
}

void EngineHost::apply(Command& c, std::int64_t nowUs) {
    switch (c.kind) {
        case CommandKind::None: break;
        case CommandKind::Document:
            if (c.doc && seq_.setDocument(*c.doc).ok()) {
                doc_ = c.doc;
                router_.setMappings(doc_->midi.mappings);
                sendClock_ = doc_->midi.sendClock;
                clockWhileStopped_ = doc_->midi.clockWhileStopped;
                sppBeforeStart_ = doc_->midi.songPositionBeforeStart;
            }
            break;
        case CommandKind::DocumentQuantized: {
            if (seq_.transport() != engine::TransportState::Playing || c.quantize == SceneQuantize::Immediate) {
                c.kind = CommandKind::Document;
                apply(c, nowUs);
                break;
            }
            const auto unit = c.quantize == SceneQuantize::NextFourBeats ? 4 * kTicksPerQuarter : kTicksPerQuarter;
            const auto now = seq_.now().value();
            pendingAt_ = MusicalTick{(now / unit + 1) * unit};
            pendingDoc_ = std::move(c.doc);
            break;
        }
        case CommandKind::Play:
            clock_.start(MusicalTick{0}, nowUs, doc_ ? doc_->settings.tempo : MilliBpm{});
            tempoMilli_.store(clock_.tempo().value());
            deferring_ = true; // D14: SPP, Start, then Clock, then tick-0 notes
            deferred_.clear();
            seq_.play(c.seed);
            deferring_ = false;
            if (sendClock_) {
                const auto pulse = midi::clockPulse();
                send(pulse.bytes.data(), pulse.size);
            }
            nextPulse_ = MusicalTick{kTicksPerMidiClock};
            for (const auto& e : deferred_) onEngineEvent(e);
            deferred_.clear();
            break;
        case CommandKind::Stop: seq_.requestStop(); break;
        case CommandKind::Continue:
            if (seq_.transport() == engine::TransportState::Paused) {
                clock_.start(seq_.now(), nowUs, clock_.tempo());
                nextPulse_ = midi::nextClockPulse(seq_.now());
                seq_.continuePlayback();
            }
            break;
        case CommandKind::Panic: seq_.panic(); break;
        case CommandKind::Switch: seq_.setSwitch(c.sw, c.on); break;
        case CommandKind::Tempo: tempo(c.tempo); break;
        case CommandKind::ArmLearn: router_.armLearn(c.learn); break;
        case CommandKind::CancelLearn: router_.cancelLearn(); break;
        case CommandKind::Output:
            if (output_) seq_.panic(); // release notes on the old device first
            output_ = std::move(c.output);
            outputAvailable_.store(output_ != nullptr);
            break;
        case CommandKind::OutputLost:
            output_.reset();
            outputAvailable_.store(false);
            seq_.panic(); // D16: flush internal note state; nothing can be sent anyway
            break;
        case CommandKind::Inputs:
            if (c.inputNames) inputNames_ = std::move(*c.inputNames);
            break;
    }
}

void EngineHost::advance(std::int64_t nowUs) {
    if (seq_.transport() != engine::TransportState::Playing) {
        // Optional free-running clock while stopped (D14 preference).
        if (sendClock_ && clockWhileStopped_ && nowUs >= nextFreePulseUs_) {
            const auto pulse = midi::clockPulse();
            send(pulse.bytes.data(), pulse.size);
            const std::int64_t perPulseUs = std::int64_t{60'000'000'000} / (std::int64_t{24} * clock_.tempo().value());
            nextFreePulseUs_ = std::max(nowUs, nextFreePulseUs_ + perPulseUs);
        }
        return;
    }

    const MusicalTick target = clock_.tickAt(nowUs);
    // MIDI Clock pulses are interleaved in tick order: events before the
    // pulse, the pulse, then events at the pulse tick.
    while (nextPulse_ <= target) {
        if (pendingDoc_ && pendingAt_ <= nextPulse_) break;
        seq_.processUntil(nextPulse_);
        if (seq_.transport() != engine::TransportState::Playing) return; // stopped on this boundary
        if (sendClock_) {
            const auto pulse = midi::clockPulse();
            send(pulse.bytes.data(), pulse.size);
        }
        nextPulse_ = nextPulse_ + MusicalTick{kTicksPerMidiClock};
    }
    if (pendingDoc_ && pendingAt_ <= target + MusicalTick{1}) { // quantized Scene recall (D35)
        seq_.processUntil(pendingAt_);
        if (seq_.setDocument(*pendingDoc_).ok()) {
            doc_ = pendingDoc_;
            router_.setMappings(doc_->midi.mappings);
        }
        pendingDoc_.reset();
        return; // continue from the recall tick on the next callback
    }
    seq_.processUntil(target + MusicalTick{1});
}

void EngineHost::publish() {
    if (--publishCountdown_ > 0) return;
    publishCountdown_ = kPublishEveryMs / kTimerPeriodMs;
    seq_.snapshotInto(scratch_);
    if (std::unique_lock lock(snapshotMutex_, std::try_to_lock); lock.owns_lock()) published_ = scratch_;
    // If the UI holds the lock, skip this one: the engine never waits.
}

void EngineHost::hiResTimerCallback() {
    const auto nowUs = nowMicros();
    while (auto c = commands_.pop()) apply(*c, nowUs);

    while (auto raw = midiIn_.pop()) {
        const auto& device = raw->device < inputNames_.size() ? inputNames_[raw->device] : juce::String();
        if (auto learned = router_.handle(raw->bytes.data(), raw->size, device.toStdString(), *this))
            learnedPending_ = std::move(learned);
    }
    if (learnedPending_) {
        if (std::unique_lock lock(learnedMutex_, std::try_to_lock); lock.owns_lock()) {
            learned_ = std::move(learnedPending_);
            learnedPending_.reset();
        } // else: retry on the next callback; the engine never waits for the UI
    }

#if SIGNALFLUX_WITH_LINK
    syncLink();
#endif
    advance(nowUs);
    publish();
}

#if SIGNALFLUX_WITH_LINK
void EngineHost::syncLink() noexcept {
    if (!link_.isEnabled()) return;
    auto state = link_.captureAudioSessionState(); // real-time safe
    const auto ours = clock_.tempo().value();
    if (ours != linkTempoSent_) { // we changed tempo locally: tell the session
        state.setTempo(ours / 1000.0, link_.clock().micros());
        link_.commitAudioSessionState(state);
        linkTempoSent_ = ours;
        return;
    }
    // Floating point only at this boundary (D25); the engine stays integer.
    const auto theirs = static_cast<std::int64_t>(state.tempo() * 1000.0 + 0.5);
    if (const auto t = MilliBpm::make(theirs); t && t->value() != ours) {
        tempo(*t);
        linkTempoSent_ = t->value();
    }
}
#endif

} // namespace signalflux::app
