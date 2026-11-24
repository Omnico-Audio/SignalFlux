// SignalFlux — document lifecycle, selection, preferences.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/AppState.h"

#include "domain/Validation.h"
#include "midi/MidiFileWriter.h"
#include "persistence/ProjectSerializer.h"

namespace signalflux::app {

namespace {
constexpr int kAutosaveMs = 60'000;          // D17
constexpr std::size_t kSeedHistory = 32;      // D29
constexpr std::size_t kCaptureMaxEvents = 400'000;
} // namespace

AppState::AppState(EngineHost& host) : host_(host) {
    juce::PropertiesFile::Options o;
    o.applicationName = "SignalFlux";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "SignalFlux";
    props_.setStorageParameters(o);
    darkTheme_ = prefs().getBoolValue("darkTheme", true);

    for (const auto& line : juce::StringArray::fromLines(prefs().getValue("seedHistory"))) {
        const auto parts = juce::StringArray::fromTokens(line, "\t", "");
        if (parts.size() >= 2)
            seeds_.push_back(SeedEntry{static_cast<std::uint64_t>(parts[0].getLargeIntValue()), parts[1],
                                       juce::Time(parts.size() > 2 ? parts[2].getLargeIntValue() : 0)});
    }

    history_.reset(makeDefaultProject());
    host_.setDocument(std::make_shared<const ProjectDocument>(doc()));
    startTimer(kAutosaveMs);
}

AppState::~AppState() {
    stopTimer();
    juce::String lines;
    for (const auto& s : seeds_)
        lines << juce::String(static_cast<juce::int64>(s.seed)) << "\t" << s.project << "\t" << s.when.toMilliseconds() << "\n";
    prefs().setValue("seedHistory", lines);
    prefs().saveIfNeeded();
    discardRecovery(); // clean exit: nothing to recover
}

bool AppState::edit(const std::function<bool(ProjectDocument&)>& change, const std::string& label, const std::string& gesture) {
    ProjectDocument next = doc();
    if (!change(next)) return false;
    if (next == doc()) return true;
    if (!validateProject(next).ok()) return false; // never commit an invalid document
    history_.commit(next, label, gesture);
    documentChanged();
    return true;
}

void AppState::performanceChange(const ProjectDocument& next) {
    if (!validateProject(next).ok()) return;
    history_.replaceCurrent(next); // saved with the project, not an undo step (D27)
    documentChanged(false);
}

void AppState::undo() {
    if (!history_.canUndo()) return;
    (void)history_.undo();
    documentChanged();
}

void AppState::redo() {
    if (!history_.canRedo()) return;
    (void)history_.redo();
    documentChanged();
}

void AppState::documentChanged(bool sendToEngine) {
    dirty_ = true;
    dirtySinceAutosave_ = true;
    if (sendToEngine) host_.setDocument(std::make_shared<const ProjectDocument>(doc()));
    // Drop a selection whose object no longer exists.
    const auto& p = doc().plots.front();
    bool exists = selection_.kind == SelectionKind::None;
    for (const auto& s : p.stations) exists |= selection_.kind == SelectionKind::Station && s.id.value() == selection_.id;
    for (const auto& f : p.flowpoints) exists |= selection_.kind == SelectionKind::Flowpoint && f.id.value() == selection_.id;
    for (const auto& g : p.gates) exists |= selection_.kind == SelectionKind::Gate && g.id.value() == selection_.id;
    for (const auto& s : p.switches) exists |= selection_.kind == SelectionKind::Switch && s.id.value() == selection_.id;
    if (!exists) selection_ = {};
    sendChangeMessage();
}

void AppState::select(Selection s) {
    if (s == selection_) return;
    selection_ = s;
    history_.endGesture();
    sendChangeMessage();
}

// --- Files ---------------------------------------------------------------------------

void AppState::newProject() {
    host_.stop();
    history_.reset(makeDefaultProject());
    file_ = juce::File();
    selection_ = {};
    host_.setDocument(std::make_shared<const ProjectDocument>(doc()));
    dirty_ = false;
    sendChangeMessage();
}

juce::String AppState::open(const juce::File& f) {
    const auto r = persistence::loadFromFile(f.getFullPathName().toStdString());
    if (!r.ok()) return juce::String(r.errors.empty() ? "Couldn't open the project." : r.errors.front());
    host_.stop(); // loading never starts MIDI (SF-010); it also stops a running project
    history_.reset(*r.document);
    file_ = f;
    selection_ = {};
    host_.setDocument(std::make_shared<const ProjectDocument>(doc()));
    dirty_ = false;
    sendChangeMessage();
    return {};
}

juce::String AppState::save() {
    if (file_ == juce::File()) return "Choose where to save the project first.";
    return saveAs(file_);
}

juce::String AppState::saveAs(const juce::File& f) {
    auto target = f.withFileExtension(persistence::kFileExtension);
    if (const auto err = persistence::saveToFile(doc(), target.getFullPathName().toStdString())) return juce::String(*err);
    file_ = target;
    dirty_ = false;
    discardRecovery();
    sendChangeMessage();
    return {};
}

juce::String AppState::title() const {
    const auto name = file_ == juce::File() ? juce::String("Untitled") : file_.getFileNameWithoutExtension();
    return name + (dirty_ ? " *" : "") + " - SignalFlux";
}

juce::File AppState::recoveryFile() const {
    return props_.getUserSettings()->getFile().getSiblingFile("Recovery.sflux");
}

void AppState::timerCallback() {
    if (!dirtySinceAutosave_) return;
    if (!persistence::saveToFile(doc(), recoveryFile().getFullPathName().toStdString())) dirtySinceAutosave_ = false;
}

std::optional<juce::File> AppState::recoveryAvailable() const {
    const auto f = recoveryFile();
    if (!f.existsAsFile()) return std::nullopt;
    return f;
}

juce::String AppState::restoreRecovery() {
    const auto f = recoveryFile();
    const auto r = persistence::loadFromFile(f.getFullPathName().toStdString());
    if (!r.ok()) return "The recovered project couldn't be read.";
    history_.reset(*r.document); // restored stopped: recovery never auto-starts playback
    file_ = juce::File();
    host_.setDocument(std::make_shared<const ProjectDocument>(doc()));
    dirty_ = true;
    sendChangeMessage();
    return {};
}

void AppState::discardRecovery() { recoveryFile().deleteFile(); }

// --- Theme and seeds -------------------------------------------------------------------

void AppState::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    prefs().setValue("darkTheme", dark);
    sendChangeMessage();
}

std::uint64_t AppState::seedForPlay() {
    const auto& s = doc().settings;
    const std::uint64_t seed = s.seedMode == SeedMode::Fixed ? s.randomSeed : static_cast<std::uint64_t>(random_.nextInt64());
    lastSeed_ = seed;
    std::erase_if(seeds_, [seed](const SeedEntry& e) { return e.seed == seed; });
    seeds_.push_front(SeedEntry{seed, file_.getFileNameWithoutExtension(), juce::Time::getCurrentTime()});
    while (seeds_.size() > kSeedHistory) seeds_.pop_back();
    sendChangeMessage();
    return seed;
}

void AppState::recallSeed(std::uint64_t seed) {
    edit([seed](ProjectDocument& d) {
        d.settings.randomSeed = seed;
        d.settings.seedMode = SeedMode::Fixed;
        return true;
    }, "Recall seed");
}

bool AppState::starred(std::uint64_t seed) const {
    const auto& s = doc().settings.starredSeeds;
    return std::find(s.begin(), s.end(), seed) != s.end();
}

void AppState::toggleStar(std::uint64_t seed) {
    edit([seed](ProjectDocument& d) {
        auto& s = d.settings.starredSeeds;
        if (const auto it = std::find(s.begin(), s.end(), seed); it != s.end()) s.erase(it);
        else s.push_back(seed);
        return true;
    }, "Star seed");
}

// --- Capture (D28) --------------------------------------------------------------------------

void AppState::pumpEngineEvents() {
    drainScratch_.clear();
    host_.drainEvents(drainScratch_);
    for (const auto& e : drainScratch_) {
        using T = engine::EngineEventType;
        if (e.type == T::TransportStart) capture_.clear(); // a capture is one run
        if (e.type != T::NoteOn && e.type != T::NoteOff && e.type != T::ControlChange && e.type != T::PitchBend) continue;
        capture_.push_back(e);
    }
    // Keep roughly the configured minutes at the current tempo.
    const auto keepTicks = static_cast<std::int64_t>(doc().settings.captureMinutes) * 60 * host_.tempo().value() *
                           kTicksPerQuarter / 60'000;
    while (!capture_.empty() && (capture_.size() > kCaptureMaxEvents ||
                                 capture_.back().tick.value() - capture_.front().tick.value() > keepTicks))
        capture_.pop_front();
}

juce::String AppState::exportCapture(const juce::File& f) {
    if (capture_.empty()) return "Nothing captured yet. Press Play first.";
    const std::vector<engine::EngineEvent> events(capture_.begin(), capture_.end());
    const auto bytes = midi::writeMidiFile(events, {doc().metadata.name, lastSeed_, host_.tempo()});
    const auto target = f.withFileExtension(".mid");
    if (!target.replaceWithData(bytes.data(), bytes.size())) return "Couldn't write " + target.getFileName() + ".";
    return {};
}

} // namespace signalflux::app
