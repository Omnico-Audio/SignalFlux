// SignalFlux — logic gate evaluation (task SF-006).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/LogicEngine.h"

#include <algorithm>
#include <variant>

namespace signalflux::engine {

namespace {

template <typename Id, typename V>
std::optional<std::size_t> findSorted(const std::vector<std::pair<Id, V>>& table, Id id) {
    const auto it = std::lower_bound(table.begin(), table.end(), id,
                                     [](const auto& e, Id key) { return e.first < key; });
    if (it == table.end() || it->first != id) return std::nullopt;
    return static_cast<std::size_t>(it - table.begin());
}

} // namespace

void LogicEngine::compile(const Plot& plot, const RuntimeGraph& graph) {
    // Remember outputs of gates that survive the edit.
    std::vector<std::pair<GateId, std::uint8_t>> previous;
    for (std::size_t i = 0; i < gates_.size(); ++i) previous.emplace_back(gates_[i].id, out_[i]);
    std::sort(previous.begin(), previous.end());

    std::vector<std::pair<SwitchId, std::uint8_t>> switches;
    for (const auto& sw : plot.switches) switches.emplace_back(sw.id, static_cast<std::uint8_t>(sw.on ? 1 : 0));
    std::sort(switches.begin(), switches.end());
    switches_ = std::move(switches);

    std::vector<const Gate*> sorted;
    for (const auto& g : plot.gates) sorted.push_back(&g);
    std::sort(sorted.begin(), sorted.end(), [](const Gate* a, const Gate* b) { return a->id < b->id; });

    std::vector<std::pair<GateId, std::uint32_t>> gateIndex;
    for (std::size_t i = 0; i < sorted.size(); ++i) gateIndex.emplace_back(sorted[i]->id, static_cast<std::uint32_t>(i));

    gates_.clear();
    driver_.assign(graph.stations().size(), -1);
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Gate& g = *sorted[i];
        Node node;
        node.id = g.id;
        node.type = g.type;
        for (const auto& in : g.inputs) {
            Input x;
            std::visit(
                [&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, StationActiveInput>) {
                        x.kind = InputKind::StationActive;
                        x.index = graph.stationIndex(v.station).value_or(0);
                    } else if constexpr (std::is_same_v<T, GateOutputInput>) {
                        x.kind = InputKind::Gate;
                        x.index = gateIndex[*findSorted(gateIndex, v.gate)].second;
                    } else if constexpr (std::is_same_v<T, SwitchInput>) {
                        x.kind = InputKind::Switch;
                        x.index = static_cast<std::uint32_t>(*findSorted(switches_, v.sw));
                    } else if constexpr (std::is_same_v<T, MidiNoteInput>) {
                        x.kind = InputKind::MidiNote;
                        x.channel = v.channel ? static_cast<std::int8_t>(v.channel->value() - 1) : std::int8_t{-1};
                        x.number = static_cast<std::uint8_t>(v.note.value());
                    } else {
                        x.kind = InputKind::MidiCc;
                        x.channel = v.channel ? static_cast<std::int8_t>(v.channel->value() - 1) : std::int8_t{-1};
                        x.number = static_cast<std::uint8_t>(v.controller.value());
                        x.threshold = static_cast<std::uint8_t>(v.threshold.value());
                    }
                },
                in);
            node.inputs.push_back(x);
        }
        for (const auto target : g.targets)
            if (const auto s = graph.stationIndex(target)) driver_[*s] = static_cast<std::int32_t>(i);
        gates_.push_back(std::move(node));
    }

    // Depth-first search from the lowest GateId; back-edges become feedback edges.
    order_.clear();
    feedbackEdges_ = 0;
    enum : std::uint8_t { Unvisited, Visiting, Done };
    std::vector<std::uint8_t> state(gates_.size(), Unvisited);
    std::vector<std::pair<std::uint32_t, std::size_t>> stack; // (gate, next input)
    for (std::uint32_t root = 0; root < gates_.size(); ++root) {
        if (state[root] != Unvisited) continue;
        stack.emplace_back(root, 0);
        state[root] = Visiting;
        while (!stack.empty()) {
            auto& [g, next] = stack.back();
            if (next < gates_[g].inputs.size()) {
                Input& in = gates_[g].inputs[next++];
                if (in.kind != InputKind::Gate) continue;
                if (state[in.index] == Visiting) {
                    in.feedback = true;
                    ++feedbackEdges_;
                } else if (state[in.index] == Unvisited) {
                    state[in.index] = Visiting;
                    stack.emplace_back(in.index, 0);
                }
                continue;
            }
            state[g] = Done;
            order_.push_back(g);
            stack.pop_back();
        }
    }

    out_.assign(gates_.size(), 0);
    for (std::size_t i = 0; i < gates_.size(); ++i)
        if (const auto p = findSorted(previous, gates_[i].id)) out_[i] = previous[*p].second;
    prev_ = out_;
}

bool LogicEngine::setSwitch(SwitchId id, bool on) noexcept {
    const auto i = findSorted(switches_, id);
    if (!i) return false;
    switches_[*i].second = on ? 1 : 0;
    return true;
}

std::optional<bool> LogicEngine::outputOf(GateId id) const noexcept {
    for (std::size_t i = 0; i < gates_.size(); ++i)
        if (gates_[i].id == id) return out_[i] != 0;
    return std::nullopt;
}

bool LogicEngine::inputValue(const Input& in, const std::vector<std::uint8_t>& stationActive,
                             const MidiInputState& midi) const noexcept {
    switch (in.kind) {
        case InputKind::StationActive:
            return in.index < stationActive.size() && stationActive[in.index] != 0;
        case InputKind::Gate:
            return (in.feedback ? prev_[in.index] : out_[in.index]) != 0;
        case InputKind::Switch:
            return switches_[in.index].second != 0;
        case InputKind::MidiNote:
            if (in.channel >= 0) return midi.noteHeld[static_cast<std::size_t>(in.channel)][in.number] != 0;
            for (const auto& ch : midi.noteHeld)
                if (ch[in.number] != 0) return true;
            return false;
        case InputKind::MidiCc:
            if (in.channel >= 0) return midi.ccValue[static_cast<std::size_t>(in.channel)][in.number] >= in.threshold;
            for (const auto& ch : midi.ccValue)
                if (ch[in.number] >= in.threshold) return true;
            return false;
    }
    return false;
}

bool LogicEngine::step(const std::vector<std::uint8_t>& stationActive, const MidiInputState& midi) noexcept {
    prev_ = out_; // same size: no allocation
    bool changed = false;
    for (const auto g : order_) {
        const Node& node = gates_[g];
        std::size_t trueCount = 0;
        for (const auto& in : node.inputs) trueCount += inputValue(in, stationActive, midi) ? 1u : 0u;
        const std::size_t n = node.inputs.size();
        bool v = false;
        switch (node.type) {
            case GateType::And:  v = trueCount == n; break;
            case GateType::Or:   v = trueCount > 0; break;
            case GateType::Xor:  v = (trueCount % 2) == 1; break;
            case GateType::Not:  v = trueCount == 0; break;
            case GateType::Nand: v = trueCount != n; break;
            case GateType::Nor:  v = trueCount == 0; break;
        }
        const std::uint8_t nv = v ? 1 : 0;
        changed = changed || nv != out_[g];
        out_[g] = nv;
    }
    return changed;
}

} // namespace signalflux::engine
