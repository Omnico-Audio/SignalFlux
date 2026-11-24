// SignalFlux — the Plot canvas (task SF-009).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "app/GridCanvas.h"

#include "domain/RouteGeometry.h"

#include <cmath>

namespace signalflux::app {

namespace {

const char* gateLabel(GateType t) {
    switch (t) {
        case GateType::And: return "AND";
        case GateType::Or: return "OR";
        case GateType::Xor: return "XOR";
        case GateType::Not: return "NOT";
        case GateType::Nand: return "NAND";
        case GateType::Nor: return "NOR";
    }
    return "?";
}

float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b) {
    juce::Point<float> nearest;
    return juce::Line<float>(a, b).getDistanceFromPoint(p, nearest);
}

} // namespace

GridCanvas::GridCanvas(AppState& state) : state_(state) {
    state_.addChangeListener(this);
    setWantsKeyboardFocus(true);
    updateSize();
    startTimerHz(60);
}

GridCanvas::~GridCanvas() { state_.removeChangeListener(this); }

void GridCanvas::updateSize() {
    const auto& p = state_.doc().plots.front();
    setSize(p.width * kCell, p.height * kCell);
}

void GridCanvas::timerCallback() {
    snap_ = state_.host().snapshot();
    snapTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    state_.pumpEngineEvents();
    repaint();
}

juce::Point<float> GridCanvas::centre(GridCoordinate c) const {
    return {(static_cast<float>(c.x) + 0.5f) * kCell, (static_cast<float>(c.y) + 0.5f) * kCell};
}

GridCoordinate GridCanvas::cellAt(juce::Point<int> p) const { return {p.x / kCell, p.y / kCell}; }

const Gate* GridCanvas::gateAt(GridCoordinate c) const {
    for (const auto& g : state_.doc().plots.front().gates)
        if (g.position == c) return &g;
    return nullptr;
}

const Switch* GridCanvas::switchAt(GridCoordinate c) const {
    for (const auto& s : state_.doc().plots.front().switches)
        if (s.position == c) return &s;
    return nullptr;
}

std::optional<FlowpointId> GridCanvas::routeNear(juce::Point<float> p) const {
    const auto& doc = state_.doc();
    for (const auto& f : doc.plots.front().flowpoints) {
        const auto* a = editing::findStation(doc, f.from);
        const auto* b = editing::findStation(doc, f.to);
        if (a == nullptr || b == nullptr) continue;
        const auto r = geometry::routeBetween(a->position, b->position, f.elbow);
        for (std::size_t i = 0; i + 1 < r.points.size(); ++i) {
            const juce::Point<float> s{static_cast<float>((r.points[i].x + 0.5) * kCell), static_cast<float>((r.points[i].y + 0.5) * kCell)};
            const juce::Point<float> t{static_cast<float>((r.points[i + 1].x + 0.5) * kCell), static_cast<float>((r.points[i + 1].y + 0.5) * kCell)};
            if (distanceToSegment(p, s, t) < 5.0f) return f.id;
        }
    }
    return std::nullopt;
}

// --- Painting -------------------------------------------------------------------------

void GridCanvas::paint(juce::Graphics& g) {
    const auto& th = state_.theme();
    g.fillAll(th.canvas);
    const auto& p = state_.doc().plots.front();
    g.setColour(th.gridDot);
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) g.fillEllipse(centre({x, y}).x - 1.2f, centre({x, y}).y - 1.2f, 2.4f, 2.4f);

    drawRoutes(g);
    drawLogic(g);
    drawStations(g);
    drawTravelers(g);

    if (connectingFrom_) {
        if (const auto* s = editing::findStation(state_.doc(), *connectingFrom_)) {
            g.setColour(th.station(s->colour).withAlpha(0.7f));
            g.drawLine(juce::Line<float>(centre(s->position), dragPoint_.toFloat()), 2.0f);
        }
    }
}

void GridCanvas::drawRoutes(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto& doc = state_.doc();
    const auto sel = state_.selection();
    for (const auto& f : doc.plots.front().flowpoints) {
        const auto* a = editing::findStation(doc, f.from);
        const auto* b = editing::findStation(doc, f.to);
        if (a == nullptr || b == nullptr) continue;
        const auto r = geometry::routeBetween(a->position, b->position, f.elbow);
        juce::Path path;
        for (std::size_t i = 0; i < r.points.size(); ++i) {
            const juce::Point<float> pt{static_cast<float>((r.points[i].x + 0.5) * kCell), static_cast<float>((r.points[i].y + 0.5) * kCell)};
            if (i == 0) path.startNewSubPath(pt);
            else path.lineTo(pt);
        }
        const bool selected = sel.kind == SelectionKind::Flowpoint && sel.id == f.id.value();
        const auto colour = th.station(a->colour); // route colour = source Station (SF-009)
        g.setColour(colour.withAlpha(f.weight.value() == 0 ? 0.25f : 0.8f));
        g.strokePath(path, juce::PathStrokeType(selected ? 3.5f : 2.0f));

        // Direction marker at the midpoint, in the source colour.
        const auto mid = geometry::pointAlong(r, 0.5);
        const auto ahead = geometry::pointAlong(r, std::min(1.0, 0.5 + 0.2 / std::max(r.length, 0.2)));
        const juce::Point<float> m{static_cast<float>((mid.x + 0.5) * kCell), static_cast<float>((mid.y + 0.5) * kCell)};
        const juce::Point<float> n{static_cast<float>((ahead.x + 0.5) * kCell), static_cast<float>((ahead.y + 0.5) * kCell)};
        juce::Path arrow;
        arrow.addTriangle(-5.0f, -5.0f, -5.0f, 5.0f, 6.0f, 0.0f);
        g.fillPath(arrow, juce::AffineTransform::rotation(std::atan2(n.y - m.y, n.x - m.x)).translated(m));

        // Beat ticks (D31) from the same integer leg duration the engine uses.
        if (const auto dur = legDuration(manhattanLength(a->position, b->position), doc.settings.gridSpacing, f.rate)) {
            for (const auto& t : geometry::beatTicks(dur->value())) {
                const auto q = geometry::pointAlong(r, t.fraction);
                const float size = t.strong ? 4.0f : 2.5f;
                g.fillEllipse(static_cast<float>((q.x + 0.5) * kCell) - size / 2, static_cast<float>((q.y + 0.5) * kCell) - size / 2, size, size);
            }
        }
    }
}

void GridCanvas::drawStations(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto sel = state_.selection();
    for (const auto& s : state_.doc().plots.front().stations) {
        const auto c = centre(s.position);
        const auto box = juce::Rectangle<float>(22.0f, 22.0f).withCentre(c);
        const auto colour = th.station(s.colour);
        bool enabled = true, active = false;
        std::uint32_t count = 0;
        for (const auto& v : snap_.stations)
            if (v.id == s.id) {
                enabled = v.enabled;
                active = v.active;
                count = v.terminatorCount;
            }
        const bool silent = s.manuallyMuted || !enabled;
        g.setColour(silent ? colour.withAlpha(0.25f) : colour);
        if (s.eventType == StationEventType::Note) g.fillRoundedRectangle(box, 5.0f);
        else g.fillEllipse(box); // CC / Pitch Bend Stations are round
        if (active) {
            g.setColour(th.text);
            g.drawRoundedRectangle(box.expanded(3.0f), 7.0f, 1.5f);
        }
        if (s.isStart) { // start marker: small triangle in the corner
            juce::Path tri;
            tri.addTriangle(box.getX() - 2, box.getY() - 2, box.getX() + 8, box.getY() - 2, box.getX() - 2, box.getY() + 8);
            g.setColour(th.text);
            g.fillPath(tri);
        }
        if (sel.kind == SelectionKind::Station && sel.id == s.id.value()) {
            g.setColour(th.selection);
            g.drawRoundedRectangle(box.expanded(5.0f), 8.0f, 2.0f);
        }
        if (s.terminatorLimit) {
            g.setColour(th.text);
            g.setFont(juce::FontOptions(10.0f));
            const auto label = *s.terminatorLimit == 0 ? juce::String("block") : juce::String(count) + "/" + juce::String(*s.terminatorLimit);
            g.drawText(label, box.translated(0, 16).expanded(10, 0).toNearestInt(), juce::Justification::centred);
        }
    }
}

void GridCanvas::drawLogic(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto sel = state_.selection();
    const auto& plot = state_.doc().plots.front();
    g.setFont(juce::FontOptions(9.0f));
    for (const auto& gate : plot.gates) {
        const auto c = centre(gate.position);
        bool out = true;
        for (const auto& v : snap_.gates)
            if (v.id == gate.id) out = v.output;
        // Dotted wires to driven Stations.
        g.setColour(th.textMuted.withAlpha(0.6f));
        for (const auto t : gate.targets)
            if (const auto* s = editing::findStation(state_.doc(), t)) {
                juce::Path wire;
                wire.startNewSubPath(c);
                wire.lineTo(centre(s->position));
                juce::Path dashed;
                const float dashes[] = {3.0f, 3.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, wire, dashes, 2);
                g.fillPath(dashed);
            }
        juce::Path diamond;
        diamond.addQuadrilateral(c.x, c.y - 13, c.x + 13, c.y, c.x, c.y + 13, c.x - 13, c.y);
        g.setColour(out ? th.text : th.textMuted.withAlpha(0.5f));
        g.fillPath(diamond);
        g.setColour(th.canvas);
        g.drawText(gateLabel(gate.type), juce::Rectangle<float>(30, 12).withCentre(c).toNearestInt(), juce::Justification::centred);
        if (sel.kind == SelectionKind::Gate && sel.id == gate.id.value()) {
            g.setColour(th.selection);
            g.strokePath(diamond, juce::PathStrokeType(2.0f), juce::AffineTransform::scale(1.25f, 1.25f, c.x, c.y));
        }
    }
    for (const auto& sw : plot.switches) {
        const auto c = centre(sw.position);
        const auto pill = juce::Rectangle<float>(26.0f, 14.0f).withCentre(c);
        g.setColour(th.textMuted);
        g.drawRoundedRectangle(pill, 7.0f, 1.5f);
        g.setColour(sw.on ? th.text : th.textMuted);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({sw.on ? pill.getRight() - 7 : pill.getX() + 7, c.y}));
        if (sel.kind == SelectionKind::Switch && sel.id == sw.id.value()) {
            g.setColour(th.selection);
            g.drawRoundedRectangle(pill.expanded(4.0f), 9.0f, 2.0f);
        }
    }
}

void GridCanvas::drawTravelers(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto& doc = state_.doc();
    // Presentation-only interpolation between engine snapshots.
    double nowTick = static_cast<double>(snap_.now.value());
    if (snap_.transport == engine::TransportState::Playing) {
        const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - snapTimeMs_;
        nowTick += elapsedMs * state_.host().tempo().value() * kTicksPerQuarter / 60'000'000.0;
    }
    for (const auto& t : snap_.travelers) {
        const auto* from = editing::findStation(doc, t.from);
        if (from == nullptr) continue;
        const auto colour = th.station(from->colour); // sprite colour = source Station
        if (t.dwelling) {
            const auto c = centre(from->position);
            g.setColour(colour.withAlpha(0.6f));
            g.drawEllipse(juce::Rectangle<float>(34.0f, 34.0f).withCentre(c), 2.0f);
            continue;
        }
        const auto* to = editing::findStation(doc, t.to);
        const Flowpoint* fp = nullptr;
        for (const auto& f : doc.plots.front().flowpoints)
            if (f.id == t.flowpoint) fp = &f;
        if (to == nullptr || fp == nullptr) continue;
        const double span = static_cast<double>((t.deadline - t.legStart).value());
        const double fraction = span > 0 ? (nowTick - static_cast<double>(t.legStart.value())) / span : 1.0;
        const auto r = geometry::routeBetween(from->position, to->position, fp->elbow);
        const auto q = geometry::pointAlong(r, fraction);
        const juce::Point<float> pos{static_cast<float>((q.x + 0.5) * kCell), static_cast<float>((q.y + 0.5) * kCell)};
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(pos));
        g.setColour(th.canvas);
        g.drawEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(pos), 2.0f);
    }
}

// --- Editing ---------------------------------------------------------------------------

void GridCanvas::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    const auto cell = cellAt(e.getPosition());
    const auto& doc = state_.doc();
    const auto* station = editing::stationAt(doc, cell);

    switch (tool_) {
        case Tool::Station:
            if (station == nullptr) {
                state_.edit([cell](ProjectDocument& d) { return editing::addStation(d, cell).has_value(); }, "Add Station");
                if (const auto* s = editing::stationAt(state_.doc(), cell)) state_.select({SelectionKind::Station, s->id.value()});
                return;
            }
            break;
        case Tool::Gate:
            if (station == nullptr && gateAt(cell) == nullptr) {
                state_.edit([cell](ProjectDocument& d) { return editing::addGate(d, cell, GateType::And).has_value(); }, "Add gate");
                if (const auto* gt = gateAt(cell)) state_.select({SelectionKind::Gate, gt->id.value()});
                return;
            }
            break;
        case Tool::Switch:
            if (station == nullptr && switchAt(cell) == nullptr) {
                state_.edit([cell](ProjectDocument& d) { return editing::addSwitch(d, cell).has_value(); }, "Add Switch");
                if (const auto* sw = switchAt(cell)) state_.select({SelectionKind::Switch, sw->id.value()});
                return;
            }
            break;
        case Tool::Select: break;
    }

    if (station != nullptr) {
        state_.select({SelectionKind::Station, station->id.value()});
        // Shift- or Alt-drag from a Station draws a new route; a plain drag moves it.
        if (e.mods.isShiftDown() || e.mods.isAltDown()) connectingFrom_ = station->id;
        else {
            dragging_ = station->id;
            ++dragGesture_;
        }
        dragPoint_ = e.getPosition();
        return;
    }
    if (const auto* gt = gateAt(cell)) return state_.select({SelectionKind::Gate, gt->id.value()});
    if (const auto* sw = switchAt(cell)) {
        state_.select({SelectionKind::Switch, sw->id.value()});
        // Clicking a Switch flips it: a performance action, not an undo step (D27).
        auto next = state_.doc();
        for (auto& s : next.plots.front().switches)
            if (s.id == sw->id) s.on = !s.on;
        const bool on = !sw->on;
        const auto id = sw->id;
        state_.performanceChange(next);
        state_.host().setSwitch(id, on);
        return;
    }
    if (const auto fp = routeNear(e.position)) return state_.select({SelectionKind::Flowpoint, fp->value()});
    state_.select({});
}

void GridCanvas::mouseDrag(const juce::MouseEvent& e) {
    dragPoint_ = e.getPosition();
    if (dragging_) {
        const auto cell = cellAt(e.getPosition());
        const auto id = *dragging_;
        state_.edit([id, cell](ProjectDocument& d) { return editing::moveStation(d, id, cell); }, "Move Station",
                    "drag-" + std::to_string(dragGesture_));
    }
    if (connectingFrom_) repaint();
}

void GridCanvas::mouseUp(const juce::MouseEvent& e) {
    if (connectingFrom_) {
        if (const auto* target = editing::stationAt(state_.doc(), cellAt(e.getPosition()))) {
            const auto from = *connectingFrom_;
            const auto to = target->id;
            state_.edit([from, to](ProjectDocument& d) { return editing::connect(d, from, to).has_value(); }, "Connect");
        }
        connectingFrom_.reset();
        repaint();
    }
    dragging_.reset();
    state_.endGesture();
}

void GridCanvas::mouseDoubleClick(const juce::MouseEvent& e) {
    if (const auto fp = routeNear(e.position)) { // double-click a route: flip its elbow
        const auto id = *fp;
        state_.edit([id](ProjectDocument& d) {
            auto* f = editing::findFlowpoint(d, id);
            if (f == nullptr) return false;
            f->elbow = f->elbow == RouteElbow::HorizontalFirst ? RouteElbow::VerticalFirst : RouteElbow::HorizontalFirst;
            return true;
        }, "Flip route");
    }
}

void GridCanvas::deleteSelection() {
    const auto sel = state_.selection();
    state_.edit([sel](ProjectDocument& d) {
        switch (sel.kind) {
            case SelectionKind::Station: return editing::deleteStation(d, StationId{sel.id});
            case SelectionKind::Flowpoint: return editing::deleteFlowpoint(d, FlowpointId{sel.id});
            case SelectionKind::Gate: return editing::deleteGate(d, GateId{sel.id});
            case SelectionKind::Switch: return editing::deleteSwitch(d, SwitchId{sel.id});
            case SelectionKind::None: return false;
        }
        return false;
    }, "Delete");
}

bool GridCanvas::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        deleteSelection();
        return true;
    }
    return false;
}

} // namespace signalflux::app
