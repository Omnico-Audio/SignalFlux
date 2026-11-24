// SignalFlux — property inspector (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/Inspector.h"

#include <variant>

namespace signalflux::app {

namespace {

using Getter = std::function<double()>;
using Setter = std::function<void(double)>;

class NumberProp final : public juce::SliderPropertyComponent {
public:
    NumberProp(const juce::String& name, double min, double max, Getter get, Setter set)
        : SliderPropertyComponent(name, min, max, 1.0), get_(std::move(get)), set_(std::move(set)) {
        refresh();
    }
    void setValue(double v) override { set_(v); }
    double getValue() const override { return get_(); }

private:
    Getter get_;
    Setter set_;
};

class ChoiceProp final : public juce::ChoicePropertyComponent {
public:
    ChoiceProp(const juce::String& name, const juce::StringArray& options, std::function<int()> get, std::function<void(int)> set)
        : ChoicePropertyComponent(name), get_(std::move(get)), set_(std::move(set)) {
        choices = options;
        refresh();
    }
    void setIndex(int i) override { set_(i); }
    int getIndex() const override { return get_(); }

private:
    std::function<int()> get_;
    std::function<void(int)> set_;
};

class BoolProp final : public juce::BooleanPropertyComponent {
public:
    BoolProp(const juce::String& name, std::function<bool()> get, std::function<void(bool)> set)
        : BooleanPropertyComponent(name, "On", "Off"), get_(std::move(get)), set_(std::move(set)) {
        refresh();
    }
    void setState(bool s) override { set_(s); }
    bool getState() const override { return get_(); }

private:
    std::function<bool()> get_;
    std::function<void(bool)> set_;
};

class TextProp final : public juce::TextPropertyComponent {
public:
    TextProp(const juce::String& name, std::function<juce::String()> get, std::function<void(const juce::String&)> set)
        : TextPropertyComponent(name, 64, false), get_(std::move(get)), set_(std::move(set)) {
        refresh();
    }
    void setText(const juce::String& t) override { set_(t); }
    juce::String getText() const override { return get_(); }

private:
    std::function<juce::String()> get_;
    std::function<void(const juce::String&)> set_;
};

class ButtonProp final : public juce::ButtonPropertyComponent {
public:
    ButtonProp(const juce::String& name, juce::String text, std::function<void()> click)
        : ButtonPropertyComponent(name, false), text_(std::move(text)), click_(std::move(click)) {
        refresh();
    }
    void buttonClicked() override { click_(); }
    juce::String getButtonText() const override { return text_; }

private:
    juce::String text_;
    std::function<void()> click_;
};

/// A drop-down whose choice triggers an action (e.g. "Add input…").
class ActionProp final : public juce::PropertyComponent {
public:
    ActionProp(const juce::String& name, const juce::StringArray& items, std::function<void(int)> chosen)
        : PropertyComponent(name), chosen_(std::move(chosen)) {
        box_.addItemList(items, 1);
        box_.setTextWhenNothingSelected("Choose...");
        box_.onChange = [this] {
            const auto i = box_.getSelectedItemIndex();
            box_.setSelectedId(0, juce::dontSendNotification);
            if (i >= 0) chosen_(i);
        };
        addAndMakeVisible(box_);
    }
    void refresh() override {}

private:
    juce::ComboBox box_;
    std::function<void(int)> chosen_;
};

const juce::StringArray kNoteValues{"1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "1/8 triplet", "1/16 triplet"};
const std::int64_t kNoteTicks[] = {120, 240, 480, 960, 1920, 3840, 7680, 320, 160};

int noteValueIndex(std::int64_t ticks) {
    for (int i = 0; i < kNoteValues.size(); ++i)
        if (kNoteTicks[i] == ticks) return i;
    return 2;
}

struct ChordPreset {
    const char* name;
    std::vector<std::int32_t> offsets;
};
const std::vector<ChordPreset> kChords{{"Single note", {}},     {"Triad", {0, 2, 4}}, {"Seventh", {0, 2, 4, 6}},
                                      {"Sus2", {0, 1, 4}},      {"Sus4", {0, 3, 4}},  {"Power (root + fifth)", {0, 4}},
                                      {"Octave", {0, 7}},       {"Add9", {0, 2, 4, 8}}};

struct RateChoice {
    const char* name;
    int num, den;
};
const std::vector<RateChoice> kRates{{"x1/4", 1, 4}, {"x1/3", 1, 3}, {"x1/2", 1, 2}, {"x2/3", 2, 3}, {"x3/4", 3, 4},
                                     {"x1", 1, 1},   {"x4/3", 4, 3}, {"x3/2", 3, 2}, {"x2", 2, 1},   {"x3", 3, 1},
                                     {"x4", 4, 1}};

juce::String describe(const GateInput& in, const ProjectDocument& doc) {
    return std::visit(
        [&](const auto& v) -> juce::String {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, StationActiveInput>) {
                const auto* s = editing::findStation(doc, v.station);
                return "Active: " + juce::String(s ? s->name : "?");
            } else if constexpr (std::is_same_v<T, GateOutputInput>) return "Gate " + juce::String(v.gate.value());
            else if constexpr (std::is_same_v<T, SwitchInput>) return "Switch " + juce::String(v.sw.value());
            else if constexpr (std::is_same_v<T, MidiNoteInput>) return "MIDI note " + juce::String(v.note.value());
            else return "MIDI CC " + juce::String(v.controller.value()) + " >= " + juce::String(v.threshold.value());
        },
        in);
}

} // namespace

Inspector::Inspector(AppState& state) : state_(state) {
    heading_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(heading_);
    addAndMakeVisible(panel_);
    state_.addChangeListener(this);
    rebuild();
}

Inspector::~Inspector() { state_.removeChangeListener(this); }

void Inspector::paint(juce::Graphics& g) {
    const auto& th = state_.theme();
    g.fillAll(th.panel);
    g.setColour(th.panelBorder);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    heading_.setColour(juce::Label::textColourId, th.text);
}

void Inspector::resized() {
    auto r = getLocalBounds().reduced(12);
    heading_.setBounds(r.removeFromTop(28));
    panel_.setBounds(r);
}

void Inspector::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (state_.selection() != shown_) rebuild();
    else panel_.refreshAll();
    repaint();
}

void Inspector::rebuild() {
    shown_ = state_.selection();
    panel_.clear();
    switch (shown_.kind) {
        case SelectionKind::Station: buildStation(StationId{shown_.id}); break;
        case SelectionKind::Flowpoint: buildFlowpoint(FlowpointId{shown_.id}); break;
        case SelectionKind::Gate: buildGate(GateId{shown_.id}); break;
        case SelectionKind::Switch: buildSwitch(SwitchId{shown_.id}); break;
        case SelectionKind::None: buildProject(); break;
    }
}

void Inspector::buildStation(StationId id) {
    heading_.setText("Station", juce::dontSendNotification);
    auto& st = state_;
    // Reads always come from the live document; writes are undoable edits.
    auto get = [&st, id]() -> const Station& { return *editing::findStation(st.doc(), id); };
    auto set = [&st, id](const char* label, std::function<void(Station&)> f) {
        st.edit([id, f](ProjectDocument& d) {
            auto* s = editing::findStation(d, id);
            if (s == nullptr) return false;
            f(*s);
            return true;
        }, label, std::string("station-") + std::to_string(id.value()) + "-" + label);
    };
    juce::Array<juce::PropertyComponent*> general, note, flow, control;

    general.add(new TextProp("Name", [get] { return juce::String(get().name); },
                             [set](const juce::String& t) { set("Name", [t](Station& s) { s.name = t.toStdString(); }); }));
    general.add(new BoolProp("Start", [get] { return get().isStart; },
                             [set](bool v) { set("Start", [v](Station& s) { s.isStart = v; }); }));
    general.add(new ChoiceProp("Colour", {"1", "2", "3", "4", "5", "6", "7", "8"}, [get] { return static_cast<int>(get().colour); },
                               [set](int i) { set("Colour", [i](Station& s) { s.colour = static_cast<std::uint8_t>(i); }); }));
    general.add(new ChoiceProp("Sends", {"Note", "Control Change", "Pitch Bend"}, [get] { return static_cast<int>(get().eventType); },
                               [set](int i) { set("Sends", [i](Station& s) { s.eventType = static_cast<StationEventType>(i); }); }));
    general.add(new NumberProp("Channel", 1, 16, [get] { return get().channel.value(); },
                               [set](double v) { set("Channel", [v](Station& s) { s.channel = *MidiChannel::make(static_cast<int>(v)); }); }));
    general.add(new BoolProp("Muted", [get] { return get().manuallyMuted; },
                             [set](bool v) { set("Mute", [v](Station& s) { s.manuallyMuted = v; }); }));
    general.add(new NumberProp("Probability %", 0, 100, [get] { return get().probability.value(); },
                               [set](double v) { set("Probability", [v](Station& s) { s.probability = *ProbabilityPercent::make(static_cast<int>(v)); }); }));
    general.add(new ButtonProp("MIDI Learn", "Learn probability", [&st, id] {
        st.host().armLearn({MappingTarget::StationProbability, id, std::nullopt});
    }));

    note.add(new ChoiceProp("Mode", {"Absolute", "Relative", "Random value"}, [get] { return static_cast<int>(get().note.mode); },
                            [set](int i) { set("Note mode", [i](Station& s) { s.note.mode = static_cast<NoteMode>(i); }); }));
    note.add(new NumberProp("Note", 0, 127, [get] { return get().note.absoluteNote.value(); },
                            [set](double v) { set("Note", [v](Station& s) { s.note.absoluteNote = *MidiNote::make(static_cast<int>(v)); }); }));
    note.add(new BoolProp("Force scale", [get] { return get().note.forceScale; },
                          [set](bool v) { set("Force scale", [v](Station& s) { s.note.forceScale = v; }); }));
    note.add(new NumberProp("Relative steps", -24, 24, [get] { return get().note.relativeSteps; },
                            [set](double v) { set("Relative steps", [v](Station& s) { s.note.relativeSteps = static_cast<int>(v); }); }));
    note.add(new BoolProp("Pass", [get] { return get().note.pass; },
                          [set](bool v) { set("Pass", [v](Station& s) { s.note.pass = v; }); }));
    note.add(new BoolProp("Relative pass-through", [get] { return get().note.relativePassThrough; },
                          [set](bool v) { set("Pass-through", [v](Station& s) { s.note.relativePassThrough = v; }); }));
    note.add(new ChoiceProp("Random", {"Anchor", "Walk"}, [get] { return static_cast<int>(get().note.random.mode); },
                            [set](int i) { set("Random mode", [i](Station& s) { s.note.random.mode = static_cast<RandomValueMode>(i); }); }));
    note.add(new NumberProp("Anchor note", 0, 127, [get] { return get().note.random.anchor.value(); },
                            [set](double v) { set("Anchor", [v](Station& s) { s.note.random.anchor = *MidiNote::make(static_cast<int>(v)); }); }));
    note.add(new NumberProp("Random range (steps)", 0, 24, [get] { return get().note.random.rangeSteps; },
                            [set](double v) { set("Range", [v](Station& s) { s.note.random.rangeSteps = static_cast<int>(v); }); }));
    note.add(new NumberProp("Walk bound (steps)", 0, 48, [get] { return get().note.random.boundSteps; },
                            [set](double v) { set("Bound", [v](Station& s) { s.note.random.boundSteps = static_cast<int>(v); }); }));
    juce::StringArray chordNames;
    for (const auto& c : kChords) chordNames.add(c.name);
    note.add(new ChoiceProp("Chord", chordNames,
                            [get] {
                                for (std::size_t i = 0; i < kChords.size(); ++i)
                                    if (kChords[i].offsets == get().note.chordOffsets) return static_cast<int>(i);
                                return 0;
                            },
                            [set](int i) { set("Chord", [i](Station& s) { s.note.chordOffsets = kChords[static_cast<std::size_t>(i)].offsets; }); }));
    note.add(new NumberProp("Velocity", 1, 127, [get] { return get().note.velocity.value(); },
                            [set](double v) { set("Velocity", [v](Station& s) { s.note.velocity = *Velocity::make(static_cast<int>(v)); }); }));
    note.add(new ButtonProp("MIDI Learn", "Learn velocity", [&st, id] {
        st.host().armLearn({MappingTarget::StationVelocity, id, std::nullopt});
    }));
    note.add(new ChoiceProp("Length", kNoteValues, [get] { return noteValueIndex(get().note.duration.value()); },
                            [set](int i) { set("Length", [i](Station& s) { s.note.duration = MusicalTick{kNoteTicks[i]}; }); }));

    flow.add(new ChoiceProp("Branching", {"Sequential", "Random (weighted)", "Instant (all)"}, [get] { return static_cast<int>(get().branchMode); },
                            [set](int i) { set("Branching", [i](Station& s) { s.branchMode = static_cast<BranchMode>(i); }); }));
    flow.add(new NumberProp("Extra repeats", 0, 16, [get] { return static_cast<double>(get().additionalRepeats); },
                            [set](double v) { set("Repeats", [v](Station& s) { s.additionalRepeats = static_cast<std::uint32_t>(v); }); }));
    flow.add(new ChoiceProp("Repeat every", [] { auto a = kNoteValues; a.insert(0, "Note length"); return a; }(),
                            [get] { return get().repeatInterval ? noteValueIndex(get().repeatInterval->value()) + 1 : 0; },
                            [set](int i) { set("Repeat interval", [i](Station& s) {
                                if (i == 0) s.repeatInterval.reset();
                                else s.repeatInterval = MusicalTick{kNoteTicks[i - 1]};
                            }); }));
    flow.add(new NumberProp("Terminator (-1 off, 0 block)", -1, 64,
                            [get] { return get().terminatorLimit ? static_cast<double>(*get().terminatorLimit) : -1.0; },
                            [set](double v) { set("Terminator", [v](Station& s) {
                                if (v < 0) s.terminatorLimit.reset();
                                else s.terminatorLimit = static_cast<std::uint32_t>(v);
                            }); }));

    control.add(new NumberProp("CC number", 0, 119, [get] { return get().controlChange.controller.value(); },
                               [set](double v) { set("CC number", [v](Station& s) { s.controlChange.controller = *CcNumber::make(static_cast<int>(v)); }); }));
    control.add(new NumberProp("CC value", 0, 127, [get] { return get().controlChange.target.value(); },
                               [set](double v) { set("CC value", [v](Station& s) { s.controlChange.target = *CcValue::make(static_cast<int>(v)); }); }));
    control.add(new BoolProp("CC ramp", [get] { return get().controlChange.ramp; },
                             [set](bool v) { set("CC ramp", [v](Station& s) { s.controlChange.ramp = v; }); }));
    control.add(new NumberProp("Pitch Bend", -8192, 8191, [get] { return get().pitchBend.target.value(); },
                               [set](double v) { set("Pitch Bend", [v](Station& s) { s.pitchBend.target = *PitchBendValue::make(static_cast<int>(v)); }); }));
    control.add(new BoolProp("Pitch Bend ramp", [get] { return get().pitchBend.ramp; },
                             [set](bool v) { set("Bend ramp", [v](Station& s) { s.pitchBend.ramp = v; }); }));

    panel_.addSection("General", general);
    panel_.addSection("Note", note);
    panel_.addSection("Flow", flow);
    panel_.addSection("Control Change / Pitch Bend", control, false);
}

void Inspector::buildFlowpoint(FlowpointId id) {
    heading_.setText("Route", juce::dontSendNotification);
    auto& st = state_;
    auto get = [&st, id]() -> const Flowpoint& {
        for (const auto& f : st.doc().plots.front().flowpoints)
            if (f.id == id) return f;
        return st.doc().plots.front().flowpoints.front();
    };
    auto set = [&st, id](const char* label, std::function<void(Flowpoint&)> f) {
        st.edit([id, f](ProjectDocument& d) {
            auto* fp = editing::findFlowpoint(d, id);
            if (fp == nullptr) return false;
            f(*fp);
            return true;
        }, label, std::string("route-") + std::to_string(id.value()) + "-" + label);
    };
    juce::Array<juce::PropertyComponent*> props;
    props.add(new NumberProp("Weight (Random)", 0, 100, [get] { return get().weight.value(); },
                             [set](double v) { set("Weight", [v](Flowpoint& f) { f.weight = *RouteWeight::make(static_cast<int>(v)); }); }));
    juce::StringArray rateNames;
    for (const auto& r : kRates) rateNames.add(r.name);
    props.add(new ChoiceProp("Speed", rateNames,
                             [get] {
                                 for (std::size_t i = 0; i < kRates.size(); ++i)
                                     if (RouteRate::make(kRates[i].num, kRates[i].den) == get().rate) return static_cast<int>(i);
                                 return 5;
                             },
                             [set](int i) { set("Speed", [i](Flowpoint& f) {
                                 const auto& r = kRates[static_cast<std::size_t>(i)];
                                 f.rate = *RouteRate::make(r.num, r.den);
                             }); }));
    props.add(new ChoiceProp("Corner", {"Horizontal first", "Vertical first"}, [get] { return static_cast<int>(get().elbow); },
                             [set](int i) { set("Corner", [i](Flowpoint& f) { f.elbow = static_cast<RouteElbow>(i); }); }));
    props.add(new ButtonProp("", "Delete route", [&st, id] {
        st.edit([id](ProjectDocument& d) { return editing::deleteFlowpoint(d, id); }, "Delete route");
    }));
    panel_.addProperties(props);
}

void Inspector::buildGate(GateId id) {
    heading_.setText("Logic gate", juce::dontSendNotification);
    auto& st = state_;
    auto findGate = [](ProjectDocument& d, GateId g) -> Gate* {
        for (auto& x : d.plots.front().gates)
            if (x.id == g) return &x;
        return nullptr;
    };
    auto get = [&st, id]() -> const Gate& {
        for (const auto& g : st.doc().plots.front().gates)
            if (g.id == id) return g;
        return st.doc().plots.front().gates.front();
    };
    auto set = [&st, id, findGate](const char* label, std::function<void(Gate&)> f) {
        st.edit([id, f, findGate](ProjectDocument& d) {
            auto* g = findGate(d, id);
            if (g == nullptr) return false;
            f(*g);
            return true;
        }, label);
    };
    const auto& doc = st.doc();
    juce::Array<juce::PropertyComponent*> props;
    props.add(new ChoiceProp("Type", {"AND", "OR", "XOR", "NOT", "NAND", "NOR"}, [get] { return static_cast<int>(get().type); },
                             [set](int i) { set("Gate type", [i](Gate& g) { g.type = static_cast<GateType>(i); }); }));

    // Inputs: summary plus an "add" menu built from what exists in the Plot.
    juce::String inputs;
    for (const auto& in : get().inputs) inputs << describe(in, doc) << "\n";
    props.add(new TextProp("Inputs", [inputs] { return inputs.isEmpty() ? juce::String("(none: output stays on)") : inputs; }, [](const juce::String&) {}));
    std::vector<GateInput> options;
    juce::StringArray names;
    for (const auto& s : doc.plots.front().stations) {
        options.emplace_back(StationActiveInput{s.id});
        names.add("Station active: " + juce::String(s.name));
    }
    for (const auto& sw : doc.plots.front().switches) {
        options.emplace_back(SwitchInput{sw.id});
        names.add("Switch: " + juce::String(sw.name));
    }
    for (const auto& g : doc.plots.front().gates) {
        options.emplace_back(GateOutputInput{g.id});
        names.add("Gate output: " + juce::String(g.name));
    }
    for (const int n : {36, 37, 38, 39, 40}) {
        options.emplace_back(MidiNoteInput{std::nullopt, *MidiNote::make(n)});
        names.add("MIDI note held: " + juce::String(n));
    }
    props.add(new ActionProp("Add input", names, [set, options](int i) {
        const auto in = options[static_cast<std::size_t>(i)];
        set("Add gate input", [in](Gate& g) { g.inputs.push_back(in); });
    }));
    props.add(new ButtonProp("", "Remove last input", [set] { set("Remove gate input", [](Gate& g) { if (!g.inputs.empty()) g.inputs.pop_back(); }); }));

    juce::String targets;
    for (const auto t : get().targets)
        if (const auto* s = editing::findStation(doc, t)) targets << s->name << "\n";
    props.add(new TextProp("Mutes / unmutes", [targets] { return targets.isEmpty() ? juce::String("(no Stations)") : targets; }, [](const juce::String&) {}));
    juce::StringArray stationNames;
    std::vector<StationId> stationIds;
    for (const auto& s : doc.plots.front().stations) {
        stationNames.add(s.name);
        stationIds.push_back(s.id);
    }
    props.add(new ActionProp("Add Station", stationNames, [&st, id, stationIds](int i) {
        const auto target = stationIds[static_cast<std::size_t>(i)];
        st.edit([id, target](ProjectDocument& d) {
            for (auto& g : d.plots.front().gates) std::erase(g.targets, target); // one driver per Station (D12)
            for (auto& g : d.plots.front().gates)
                if (g.id == id) g.targets.push_back(target);
            return true;
        }, "Gate target");
    }));
    props.add(new ButtonProp("", "Clear Stations", [set] { set("Clear gate targets", [](Gate& g) { g.targets.clear(); }); }));
    props.add(new ButtonProp("", "Delete gate", [&st, id] { st.edit([id](ProjectDocument& d) { return editing::deleteGate(d, id); }, "Delete gate"); }));
    panel_.addProperties(props);
}

void Inspector::buildSwitch(SwitchId id) {
    heading_.setText("Switch", juce::dontSendNotification);
    auto& st = state_;
    juce::Array<juce::PropertyComponent*> props;
    props.add(new TextProp("Name",
                           [&st, id] {
                               for (const auto& s : st.doc().plots.front().switches)
                                   if (s.id == id) return juce::String(s.name);
                               return juce::String();
                           },
                           [&st, id](const juce::String& t) {
                               st.edit([id, t](ProjectDocument& d) {
                                   for (auto& s : d.plots.front().switches)
                                       if (s.id == id) s.name = t.toStdString();
                                   return true;
                               }, "Rename Switch");
                           }));
    props.add(new ButtonProp("", "Delete Switch", [&st, id] { st.edit([id](ProjectDocument& d) { return editing::deleteSwitch(d, id); }, "Delete Switch"); }));
    panel_.addProperties(props);
}

void Inspector::buildProject() {
    heading_.setText("Project", juce::dontSendNotification);
    auto& st = state_;
    auto set = [&st](const char* label, std::function<void(ProjectDocument&)> f) {
        st.edit([f](ProjectDocument& d) { f(d); return true; }, label, std::string("project-") + label);
    };
    juce::Array<juce::PropertyComponent*> props;
    props.add(new TextProp("Name", [&st] { return juce::String(st.doc().metadata.name); },
                           [set](const juce::String& t) { set("Name", [t](ProjectDocument& d) { d.metadata.name = t.toStdString(); }); }));
    props.add(new NumberProp("Tempo (BPM)", 20, 300, [&st] { return st.doc().settings.tempo.value() / 1000.0; },
                             [set, &st](double v) {
                                 const auto t = *MilliBpm::make(static_cast<std::int64_t>(v) * 1000);
                                 set("Tempo", [t](ProjectDocument& d) { d.settings.tempo = t; });
                                 st.host().setTempo(t);
                             }));
    props.add(new ButtonProp("MIDI Learn", "Learn tempo", [&st] { st.host().armLearn({MappingTarget::Tempo, std::nullopt, std::nullopt}); }));
    props.add(new ButtonProp("MIDI Learn", "Learn transpose", [&st] { st.host().armLearn({MappingTarget::GlobalTranspose, std::nullopt, std::nullopt}); }));
    props.add(new ChoiceProp("Grid cell", kNoteValues, [&st] { return noteValueIndex(st.doc().settings.gridSpacing.ticks()); },
                             [set](int i) { set("Grid spacing", [i](ProjectDocument& d) { d.settings.gridSpacing = *GridSpacing::make(kNoteTicks[i]); }); }));
    props.add(new ChoiceProp("Root", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
                             [&st] { return st.doc().settings.root.value(); },
                             [set](int i) { set("Root", [i](ProjectDocument& d) { d.settings.root = *PitchClass::make(i); }); }));
    juce::StringArray scales;
    for (const auto& p : kScalePresets) scales.add(juce::String(std::string(p.name)));
    props.add(new ChoiceProp("Scale", scales,
                             [&st] {
                                 const auto p = presetFor(st.doc().settings.scale);
                                 return p ? static_cast<int>(*p) : 0;
                             },
                             [set](int i) { set("Scale", [i](ProjectDocument& d) { d.settings.scale = maskFor(static_cast<ScalePreset>(i)); }); }));
    props.add(new BoolProp("Lock seed", [&st] { return st.doc().settings.seedMode == SeedMode::Fixed; },
                           [set](bool v) { set("Seed mode", [v](ProjectDocument& d) { d.settings.seedMode = v ? SeedMode::Fixed : SeedMode::NewEachPlay; }); }));
    props.add(new NumberProp("Traveler cap", 1, 256, [&st] { return st.doc().settings.travelerCap; },
                             [set](double v) { set("Traveler cap", [v](ProjectDocument& d) { d.settings.travelerCap = static_cast<std::uint32_t>(v); }); }));
    props.add(new BoolProp("Send MIDI Clock", [&st] { return st.doc().midi.sendClock; },
                           [set](bool v) { set("Send clock", [v](ProjectDocument& d) { d.midi.sendClock = v; }); }));
    props.add(new BoolProp("Clock while stopped", [&st] { return st.doc().midi.clockWhileStopped; },
                           [set](bool v) { set("Clock while stopped", [v](ProjectDocument& d) { d.midi.clockWhileStopped = v; }); }));
    props.add(new BoolProp("Ableton Link", [&st] { return st.doc().settings.linkEnabled; },
                           [set](bool v) { set("Link", [v](ProjectDocument& d) { d.settings.linkEnabled = v; }); }));
    props.add(new ChoiceProp("Scene timing", {"Immediately", "Next beat", "Next 4 beats"},
                             [&st] { return static_cast<int>(st.doc().settings.sceneQuantize); },
                             [set](int i) { set("Scene timing", [i](ProjectDocument& d) { d.settings.sceneQuantize = static_cast<SceneQuantize>(i); }); }));
    props.add(new NumberProp("Capture (minutes)", 1, 60, [&st] { return st.doc().settings.captureMinutes; },
                             [set](double v) { set("Capture length", [v](ProjectDocument& d) { d.settings.captureMinutes = static_cast<std::uint32_t>(v); }); }));
    panel_.addProperties(props);
}

} // namespace signalflux::app
