// SignalFlux — logic gate evaluation (task SF-006; DECISIONS D12).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// A separate subsystem: it reads boolean signals (Station Active, Switches,
// MIDI input, other gates) and produces one thing, each driven Station's
// enable state. It never touches Travelers or routing.
//
// Evaluation model (D12):
//  - A gate with no inputs outputs true (it mutes nothing) until it is wired.
//  - One evaluation step computes every gate. Acyclic parts settle within the
//    step (topological order). Each cycle is broken at a deterministic feedback
//    edge: the first back-edge found by depth-first search from the lowest
//    GateId. A feedback edge reads the previous step's value.
//  - The Sequencer runs a step at the end of a processed tick when an input
//    changed or the previous step changed an output (so feedback loops keep
//    evolving), and immediately on external inputs (Switch, MIDI). Arrivals at
//    tick T therefore see the state from before T (D21).
//
// Thread ownership: engine thread. step() does not allocate.

#pragma once

#include "domain/Project.h"
#include "engine/RuntimeGraph.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace signalflux::engine {

/// Latest MIDI input state relevant to gates (D15). Channel index 0–15.
struct MidiInputState {
    std::array<std::array<std::uint8_t, 128>, 16> noteHeld{};
    std::array<std::array<std::uint8_t, 128>, 16> ccValue{};
};

class LogicEngine {
public:
    /// Builds the gate graph for `plot`, resolving Station references through
    /// `graph`. Gate outputs survive recompilation for gates that still exist.
    /// May allocate; call from setDocument, not from the deadline path.
    void compile(const Plot& plot, const RuntimeGraph& graph);

    /// One evaluation step. `stationActive[i]` is Station index i's Active
    /// signal. Returns true if any gate output changed.
    bool step(const std::vector<std::uint8_t>& stationActive, const MidiInputState& midi) noexcept;

    /// True unless the Station is driven by a gate whose output is false.
    [[nodiscard]] bool stationEnabled(StationIndex s) const noexcept {
        return s >= driver_.size() || driver_[s] < 0 || out_[static_cast<std::size_t>(driver_[s])] != 0;
    }

    /// Performance toggle of a Switch without recompiling. Returns false if unknown.
    bool setSwitch(SwitchId id, bool on) noexcept;

    [[nodiscard]] std::size_t gateCount() const noexcept { return gates_.size(); }
    [[nodiscard]] GateId gateId(std::size_t i) const noexcept { return gates_[i].id; }
    [[nodiscard]] bool output(std::size_t i) const noexcept { return out_[i] != 0; }
    [[nodiscard]] std::optional<bool> outputOf(GateId id) const noexcept;
    [[nodiscard]] bool hasFeedback() const noexcept { return feedbackEdges_ > 0; }

private:
    enum class InputKind : std::uint8_t { StationActive, Gate, Switch, MidiNote, MidiCc };
    struct Input {
        InputKind kind = InputKind::Switch;
        std::uint32_t index = 0;      ///< Station index, gate index, or switch index.
        bool feedback = false;        ///< Gate input read from the previous step.
        std::int8_t channel = -1;     ///< MIDI: 0–15, or -1 for any channel.
        std::uint8_t number = 0;      ///< MIDI note or controller.
        std::uint8_t threshold = 0;   ///< MIDI CC: on when value >= threshold.
    };
    struct Node {
        GateId id;
        GateType type = GateType::And;
        std::vector<Input> inputs;
    };

    [[nodiscard]] bool inputValue(const Input& in, const std::vector<std::uint8_t>& stationActive,
                                  const MidiInputState& midi) const noexcept;

    std::vector<Node> gates_;                  // sorted by GateId
    std::vector<std::uint32_t> order_;         // evaluation order (dependencies first)
    std::vector<std::uint8_t> out_, prev_;
    std::vector<std::int32_t> driver_;         // per Station index: gate index or -1
    std::vector<std::pair<SwitchId, std::uint8_t>> switches_; // sorted by id
    std::size_t feedbackEdges_ = 0;
};

} // namespace signalflux::engine
