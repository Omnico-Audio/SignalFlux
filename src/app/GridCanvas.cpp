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

void GridCanvas::setTool(Tool t) {
    tool_ = t;
    connectingFrom_.reset();
    connectByClicks_ = false;
    setMouseCursor(t == Tool::Select ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
    if (onToolChanged) onToolChanged(t);
    if (onHintChanged) onHintChanged();
    repaint();
}

juce::String GridCanvas::hint() const {
    if (connectingFrom_)
        return connectByClicks_ ? "Now click the Station to connect to. Esc cancels."
                                : "Release over another Station to connect them.";
    switch (tool_) {
        case Tool::Select:
            return "Drag Stations to move them. Double-click an empty cell to add one. Shift-drag between Stations to "
                   "connect. Right-click for options.";
        case Tool::Station: return "Click an empty cell to add a Station.";
        case Tool::Connect: return "Click a Station, then the Station it should send Travelers to (or drag between them).";
        case Tool::Gate: return "Click an empty cell to add a logic gate. Wire it up in the inspector on the right.";
        case Tool::Switch: return "Click an empty cell to add a Switch. Click a Switch to flip it.";
    }
    return {};
}

void GridCanvas::zoom(int steps) {
    cell_ = juce::jlimit(20, 72, cell_ + steps * 4);
    updateSize();
    repaint();
}

void GridCanvas::updateSize() {
    const auto& p = state_.doc().plots.front();
    setSize(p.width * cell_ + 2 * kMargin, p.height * cell_ + 2 * kMargin);
}

juce::Point<int> GridCanvas::centreOfContent() const {
    const auto& p = state_.doc().plots.front();
    if (p.stations.empty()) return {kMargin, kMargin};
    int minX = p.width, minY = p.height, maxX = 0, maxY = 0;
    for (const auto& s : p.stations) {
        minX = std::min(minX, s.position.x);
        minY = std::min(minY, s.position.y);
        maxX = std::max(maxX, s.position.x);
        maxY = std::max(maxY, s.position.y);
    }
    return centre({(minX + maxX) / 2, (minY + maxY) / 2}).toInt();
}

void GridCanvas::timerCallback() {
    snap_ = state_.host().snapshot();
    snapTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    state_.pumpEngineEvents();
    repaint();
}

juce::Point<float> GridCanvas::toScreen(double cellX, double cellY) const {
    return {static_cast<float>(kMargin + (cellX + 0.5) * cell_), static_cast<float>(kMargin + (cellY + 0.5) * cell_)};
}

juce::Point<float> GridCanvas::centre(GridCoordinate c) const { return toScreen(c.x, c.y); }

std::optional<GridCoordinate> GridCanvas::cellAt(juce::Point<int> p) const {
    const auto& plot = state_.doc().plots.front();
    const int x = (p.x - kMargin) / cell_, y = (p.y - kMargin) / cell_;
    if (p.x < kMargin || p.y < kMargin || x >= plot.width || y >= plot.height) return std::nullopt;
    return GridCoordinate{x, y};
}

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
    for (const auto& f : state_.doc().plots.front().flowpoints)
        if (const auto r = screenRoute(f))
            for (std::size_t i = 0; i + 1 < r->points.size(); ++i)
                if (distanceToSegment(p, r->points[i], r->points[i + 1]) < 5.0f) return f.id;
    return std::nullopt;
}

juce::Point<float> GridCanvas::ScreenRoute::along(double fraction) const {
    float d = static_cast<float>(std::clamp(fraction, 0.0, 1.0)) * length;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const float seg = points[i].getDistanceFrom(points[i + 1]);
        if (d <= seg || i + 2 == points.size())
            return seg > 0.0f ? points[i] + (points[i + 1] - points[i]) * std::min(d / seg, 1.0f) : points[i];
        d -= seg;
    }
    return points.back();
}

std::optional<GridCanvas::ScreenRoute> GridCanvas::screenRoute(const Flowpoint& f) const {
    const auto& doc = state_.doc();
    const auto* a = editing::findStation(doc, f.from);
    const auto* b = editing::findStation(doc, f.to);
    if (a == nullptr || b == nullptr) return std::nullopt;
    const auto r = geometry::routeBetween(a->position, b->position, f.elbow);
    ScreenRoute s;
    for (std::size_t i = 0; i < 3; ++i) s.points[i] = toScreen(r.points[i].x, r.points[i].y);

    bool twoWay = false;
    for (const auto& other : doc.plots.front().flowpoints) twoWay |= other.from == f.to && other.to == f.from;
    if (twoWay) {
        const float off = static_cast<float>(cell_) * 0.12f;
        auto rightNormal = [off](juce::Point<float> p, juce::Point<float> q) {
            const auto d = q - p;
            const float len = std::hypot(d.x, d.y);
            return len > 0.0f ? juce::Point<float>(-d.y / len * off, d.x / len * off) : juce::Point<float>();
        };
        const auto n0 = rightNormal(s.points[0], s.points[1]);
        const auto n1 = rightNormal(s.points[1], s.points[2]);
        const auto first = n0.isOrigin() ? n1 : n0;
        const auto second = n1.isOrigin() ? n0 : n1;
        s.points[0] += first;
        s.points[1] += (n0.isOrigin() || n1.isOrigin()) ? first : n0 + n1;
        s.points[2] += second;
    }
    s.length = s.points[0].getDistanceFrom(s.points[1]) + s.points[1].getDistanceFrom(s.points[2]);
    return s;
}

// --- Painting -------------------------------------------------------------------------

void GridCanvas::paint(juce::Graphics& g) {
    const auto& th = state_.theme();
    g.fillAll(th.canvas);
    const auto& p = state_.doc().plots.front();
    // Plot boundary and grid dots.
    g.setColour(th.gridDot.withAlpha(0.6f));
    g.drawRect(juce::Rectangle<float>(static_cast<float>(kMargin), static_cast<float>(kMargin),
                                      static_cast<float>(p.width * cell_), static_cast<float>(p.height * cell_)), 1.0f);
    g.setColour(th.gridDot);
    const auto clip = g.getClipBounds();
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            const auto c = centre({x, y});
            if (clip.contains(c.toInt())) g.fillEllipse(c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
        }
    if (hoverCell_ && tool_ != Tool::Select && tool_ != Tool::Connect) {
        g.setColour(th.text.withAlpha(0.12f));
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(cell_) - 6, static_cast<float>(cell_) - 6)
                                   .withCentre(centre(*hoverCell_)), 6.0f);
    }

    drawRoutes(g);
    drawLogic(g);
    drawStations(g);
    drawTravelers(g);

    if (connectingFrom_) {
        if (const auto* s = editing::findStation(state_.doc(), *connectingFrom_)) {
            g.setColour(th.station(s->colour).withAlpha(0.8f));
            const float dashes[] = {6.0f, 4.0f};
            g.drawDashedLine(juce::Line<float>(centre(s->position), dragPoint_.toFloat()), dashes, 2, 2.0f);
        }
    }

    if (p.stations.empty()) {
        g.setColour(th.textMuted);
        g.setFont(juce::FontOptions(18.0f));
        g.drawText("Double-click anywhere to add your first Station", getLocalBounds().withHeight(120).withY(kMargin + 2 * cell_),
                   juce::Justification::centred);
    }
}

void GridCanvas::drawRoutes(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto& doc = state_.doc();
    const auto sel = state_.selection();
    const float scale = static_cast<float>(cell_) / 40.0f;
    for (const auto& f : doc.plots.front().flowpoints) {
        const auto r = screenRoute(f);
        if (!r) continue;
        const auto* a = editing::findStation(doc, f.from);
        const auto* b = editing::findStation(doc, f.to);
        juce::Path path;
        path.startNewSubPath(r->points[0]);
        path.lineTo(r->points[1]);
        path.lineTo(r->points[2]);
        const bool selected = sel.kind == SelectionKind::Flowpoint && sel.id == f.id.value();
        const auto colour = th.station(a->colour); // route colour = source Station (SF-009)
        if (selected) {
            g.setColour(th.selection.withAlpha(0.35f));
            g.strokePath(path, juce::PathStrokeType(8.0f * scale));
        }
        g.setColour(colour.withAlpha(f.weight.value() == 0 ? 0.3f : 0.9f));
        g.strokePath(path, juce::PathStrokeType(2.5f * scale));

        // Direction marker in the source colour, mid-route.
        const auto m = r->along(0.5);
        const auto n = r->along(std::min(1.0, 0.5 + 4.0 / std::max(r->length, 4.0f)));
        juce::Path arrow;
        arrow.addTriangle(-6.0f * scale, -5.0f * scale, -6.0f * scale, 5.0f * scale, 7.0f * scale, 0.0f);
        g.fillPath(arrow, juce::AffineTransform::rotation(std::atan2(n.y - m.y, n.x - m.x)).translated(m));

        // Beat ticks (D31) from the same integer leg duration the engine uses.
        if (const auto dur = legDuration(manhattanLength(a->position, b->position), doc.settings.gridSpacing, f.rate)) {
            for (const auto& t : geometry::beatTicks(dur->value())) {
                const auto q = r->along(t.fraction);
                const float size = (t.strong ? 5.0f : 3.0f) * scale;
                g.fillEllipse(q.x - size / 2, q.y - size / 2, size, size);
            }
        }
    }
}

void GridCanvas::drawStations(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto sel = state_.selection();
    const float size = static_cast<float>(cell_) * 0.62f;
    for (const auto& s : state_.doc().plots.front().stations) {
        const auto c = centre(s.position);
        const auto box = juce::Rectangle<float>(size, size).withCentre(c);
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
        if (active) { // sounding: a soft halo
            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(box.expanded(5.0f), 9.0f);
        }
        g.setColour(silent ? colour.withAlpha(0.3f) : colour);
        if (s.eventType == StationEventType::Note) g.fillRoundedRectangle(box, size * 0.22f);
        else g.fillEllipse(box); // CC / Pitch Bend Stations are round
        if (silent) {
            g.setColour(colour);
            g.drawRoundedRectangle(box, size * 0.22f, 1.5f);
        }
        if (s.isStart) { // Start Station: a play triangle inside
            juce::Path tri;
            const float t = size * 0.22f;
            tri.addTriangle(c.x - t * 0.8f, c.y - t, c.x - t * 0.8f, c.y + t, c.x + t, c.y);
            g.setColour(th.canvas.withAlpha(0.85f));
            g.fillPath(tri);
        }
        if (sel.kind == SelectionKind::Station && sel.id == s.id.value()) {
            g.setColour(th.selection);
            g.drawRoundedRectangle(box.expanded(5.0f), size * 0.3f, 2.0f);
        }
        if (s.terminatorLimit) {
            g.setColour(th.text);
            g.setFont(juce::FontOptions(10.0f));
            const auto label = *s.terminatorLimit == 0 ? juce::String("block") : juce::String(count) + "/" + juce::String(*s.terminatorLimit);
            g.drawText(label, box.translated(0, size * 0.75f).expanded(14, 0).toNearestInt(), juce::Justification::centred);
        }
    }
}

void GridCanvas::drawLogic(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto sel = state_.selection();
    const auto& plot = state_.doc().plots.front();
    const float half = static_cast<float>(cell_) * 0.38f;
    g.setFont(juce::FontOptions(9.0f));
    for (const auto& gate : plot.gates) {
        const auto c = centre(gate.position);
        bool out = true;
        for (const auto& v : snap_.gates)
            if (v.id == gate.id) out = v.output;
        g.setColour(th.textMuted.withAlpha(0.6f)); // dotted wires to driven Stations
        for (const auto t : gate.targets)
            if (const auto* s = editing::findStation(state_.doc(), t)) {
                const float dashes[] = {3.0f, 3.0f};
                g.drawDashedLine(juce::Line<float>(c, centre(s->position)), dashes, 2, 1.0f);
            }
        juce::Path diamond;
        diamond.addQuadrilateral(c.x, c.y - half, c.x + half, c.y, c.x, c.y + half, c.x - half, c.y);
        g.setColour(out ? th.text : th.textMuted.withAlpha(0.5f));
        g.fillPath(diamond);
        g.setColour(th.canvas);
        g.drawText(gateLabel(gate.type), juce::Rectangle<float>(half * 2, 12).withCentre(c).toNearestInt(), juce::Justification::centred);
        if (sel.kind == SelectionKind::Gate && sel.id == gate.id.value()) {
            g.setColour(th.selection);
            g.strokePath(diamond, juce::PathStrokeType(2.0f), juce::AffineTransform::scale(1.25f, 1.25f, c.x, c.y));
        }
    }
    for (const auto& sw : plot.switches) {
        const auto c = centre(sw.position);
        const auto pill = juce::Rectangle<float>(half * 1.6f, half * 0.9f).withCentre(c);
        g.setColour(th.textMuted);
        g.drawRoundedRectangle(pill, pill.getHeight() / 2, 1.5f);
        const float knob = pill.getHeight() - 4;
        g.setColour(sw.on ? th.text : th.textMuted);
        g.fillEllipse(juce::Rectangle<float>(knob, knob).withCentre({sw.on ? pill.getRight() - knob / 2 - 2 : pill.getX() + knob / 2 + 2, c.y}));
        if (sel.kind == SelectionKind::Switch && sel.id == sw.id.value()) {
            g.setColour(th.selection);
            g.drawRoundedRectangle(pill.expanded(4.0f), pill.getHeight() / 2 + 4, 2.0f);
        }
    }
}

void GridCanvas::drawTravelers(juce::Graphics& g) {
    const auto& th = state_.theme();
    const auto& doc = state_.doc();
    const float dot = static_cast<float>(cell_) * 0.32f;
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
            g.setColour(colour.withAlpha(0.7f));
            g.drawEllipse(juce::Rectangle<float>(static_cast<float>(cell_) * 0.9f, static_cast<float>(cell_) * 0.9f).withCentre(centre(from->position)), 2.0f);
            continue;
        }
        const Flowpoint* fp = nullptr;
        for (const auto& f : doc.plots.front().flowpoints)
            if (f.id == t.flowpoint) fp = &f;
        const auto r = fp != nullptr ? screenRoute(*fp) : std::nullopt;
        if (!r) continue;
        const double span = static_cast<double>((t.deadline - t.legStart).value());
        const double fraction = span > 0 ? (nowTick - static_cast<double>(t.legStart.value())) / span : 1.0;
        const auto pos = r->along(fraction);
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(pos));
        g.setColour(th.canvas);
        g.drawEllipse(juce::Rectangle<float>(dot, dot).withCentre(pos), 2.0f);
    }
}

// --- Editing ---------------------------------------------------------------------------

void GridCanvas::addStationAt(GridCoordinate cell) {
    state_.edit([cell](ProjectDocument& d) { return editing::addStation(d, cell).has_value(); }, "Add Station");
    if (const auto* s = editing::stationAt(state_.doc(), cell)) state_.select({SelectionKind::Station, s->id.value()});
}

void GridCanvas::connect(StationId from, StationId to) {
    if (from == to) return;
    state_.edit([from, to](ProjectDocument& d) { return editing::connect(d, from, to).has_value(); }, "Connect");
}

void GridCanvas::flipSwitch(SwitchId id) {
    // Flipping a Switch is a performance action, not an undo step (D27).
    auto next = state_.doc();
    bool on = false;
    for (auto& s : next.plots.front().switches)
        if (s.id == id) on = s.on = !s.on;
    state_.performanceChange(next);
    state_.host().setSwitch(id, on);
}

void GridCanvas::mouseMove(const juce::MouseEvent& e) {
    const auto cell = cellAt(e.getPosition());
    if (cell != hoverCell_) {
        hoverCell_ = cell;
        repaint();
    }
    if (connectingFrom_) {
        dragPoint_ = e.getPosition();
        repaint();
    }
}

void GridCanvas::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu()) return showContextMenu(e);

    dragPoint_ = e.getPosition();
    const auto cell = cellAt(e.getPosition());
    const auto& doc = state_.doc();
    const auto* station = cell ? editing::stationAt(doc, *cell) : nullptr;

    // Second click of a click-click connection (Connect tool or "Connect from here").
    if (connectingFrom_ && connectByClicks_) {
        if (station != nullptr) connect(*connectingFrom_, station->id);
        connectingFrom_.reset();
        connectByClicks_ = false;
        if (onHintChanged) onHintChanged();
        repaint();
        return;
    }

    if (cell && station == nullptr && gateAt(*cell) == nullptr && switchAt(*cell) == nullptr) {
        switch (tool_) {
            case Tool::Station: return addStationAt(*cell);
            case Tool::Gate:
                state_.edit([c = *cell](ProjectDocument& d) { return editing::addGate(d, c, GateType::And).has_value(); }, "Add gate");
                if (const auto* gt = gateAt(*cell)) state_.select({SelectionKind::Gate, gt->id.value()});
                return;
            case Tool::Switch:
                state_.edit([c = *cell](ProjectDocument& d) { return editing::addSwitch(d, c).has_value(); }, "Add Switch");
                if (const auto* sw = switchAt(*cell)) state_.select({SelectionKind::Switch, sw->id.value()});
                return;
            case Tool::Select:
            case Tool::Connect: break;
        }
    }

    if (station != nullptr) {
        state_.select({SelectionKind::Station, station->id.value()});
        if (tool_ == Tool::Connect || e.mods.isShiftDown() || e.mods.isAltDown()) {
            connectingFrom_ = station->id; // drag to a Station, or click it next
            connectByClicks_ = false;
        } else {
            dragging_ = station->id;
            ++dragGesture_;
        }
        if (onHintChanged) onHintChanged();
        return;
    }
    if (cell) {
        if (const auto* gt = gateAt(*cell)) return state_.select({SelectionKind::Gate, gt->id.value()});
        if (const auto* sw = switchAt(*cell)) {
            state_.select({SelectionKind::Switch, sw->id.value()});
            return flipSwitch(sw->id);
        }
    }
    if (const auto fp = routeNear(e.position)) return state_.select({SelectionKind::Flowpoint, fp->value()});
    state_.select({});
}

void GridCanvas::mouseDrag(const juce::MouseEvent& e) {
    dragPoint_ = e.getPosition();
    if (dragging_) {
        if (const auto cell = cellAt(e.getPosition())) {
            const auto id = *dragging_;
            state_.edit([id, c = *cell](ProjectDocument& d) { return editing::moveStation(d, id, c); }, "Move Station",
                        "drag-" + std::to_string(dragGesture_));
        }
    }
    if (connectingFrom_) repaint();
}

void GridCanvas::mouseUp(const juce::MouseEvent& e) {
    if (connectingFrom_ && !connectByClicks_) {
        const auto cell = cellAt(e.getPosition());
        const auto* target = cell ? editing::stationAt(state_.doc(), *cell) : nullptr;
        if (target != nullptr && target->id != *connectingFrom_) {
            connect(*connectingFrom_, target->id);
            connectingFrom_.reset();
        } else if (e.getDistanceFromDragStart() < 4) {
            connectByClicks_ = true; // a plain click on the first Station: wait for the second click
        } else {
            connectingFrom_.reset();
        }
        if (onHintChanged) onHintChanged();
        repaint();
    }
    dragging_.reset();
    state_.endGesture();
}

void GridCanvas::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    const auto cell = cellAt(e.getPosition());
    if (const auto fp = routeNear(e.position); fp && (!cell || editing::stationAt(state_.doc(), *cell) == nullptr)) {
        const auto id = *fp; // double-click a route: flip its corner
        state_.edit([id](ProjectDocument& d) {
            auto* f = editing::findFlowpoint(d, id);
            if (f == nullptr) return false;
            f->elbow = f->elbow == RouteElbow::HorizontalFirst ? RouteElbow::VerticalFirst : RouteElbow::HorizontalFirst;
            return true;
        }, "Flip route");
        return;
    }
    if (tool_ == Tool::Select && cell && editing::stationAt(state_.doc(), *cell) == nullptr && gateAt(*cell) == nullptr &&
        switchAt(*cell) == nullptr)
        addStationAt(*cell);
}

void GridCanvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        zoom(wheel.deltaY > 0 ? 1 : -1);
        return;
    }
    Component::mouseWheelMove(e, wheel); // let the viewport scroll
}

void GridCanvas::showContextMenu(const juce::MouseEvent& e) {
    const auto cell = cellAt(e.getPosition());
    const auto& doc = state_.doc();
    juce::PopupMenu menu;
    juce::Component::SafePointer<GridCanvas> self(this);
    auto edit = [this](const char* label, std::function<bool(ProjectDocument&)> f) { state_.edit(f, label); };

    if (const auto* s = cell ? editing::stationAt(doc, *cell) : nullptr) {
        const auto id = s->id;
        state_.select({SelectionKind::Station, id.value()});
        menu.addSectionHeader(s->name);
        menu.addItem("Connect from here...", [self, id] {
            if (self == nullptr) return;
            self->connectingFrom_ = id;
            self->connectByClicks_ = true;
            if (self->onHintChanged) self->onHintChanged();
        });
        menu.addItem("Start Station", true, s->isStart, [edit, id] {
            edit("Start", [id](ProjectDocument& d) { auto* x = editing::findStation(d, id); x->isStart = !x->isStart; return true; });
        });
        menu.addItem("Muted", true, s->manuallyMuted, [edit, id] {
            edit("Mute", [id](ProjectDocument& d) { auto* x = editing::findStation(d, id); x->manuallyMuted = !x->manuallyMuted; return true; });
        });
        juce::PopupMenu branching;
        const char* modes[] = {"Sequential", "Random (weighted)", "Instant (all at once)"};
        for (int i = 0; i < 3; ++i)
            branching.addItem(modes[i], true, static_cast<int>(s->branchMode) == i, [edit, id, i] {
                edit("Branching", [id, i](ProjectDocument& d) { editing::findStation(d, id)->branchMode = static_cast<BranchMode>(i); return true; });
            });
        menu.addSubMenu("Branching", branching);
        juce::PopupMenu colours;
        for (int i = 0; i < 8; ++i)
            colours.addColouredItem(100 + i, "Colour " + juce::String(i + 1), state_.theme().station(i), true, s->colour == i, nullptr);
        menu.addSubMenu("Colour", colours);
        menu.addSeparator();
        menu.addItem("Delete Station", [edit, id] { edit("Delete Station", [id](ProjectDocument& d) { return editing::deleteStation(d, id); }); });
        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(), [edit, id](int r) {
            if (r >= 100 && r < 108)
                edit("Colour", [id, r](ProjectDocument& d) { editing::findStation(d, id)->colour = static_cast<std::uint8_t>(r - 100); return true; });
        });
        return;
    }
    if (const auto* gt = cell ? gateAt(*cell) : nullptr) {
        const auto id = gt->id;
        state_.select({SelectionKind::Gate, id.value()});
        juce::PopupMenu types;
        const char* names[] = {"AND", "OR", "XOR", "NOT", "NAND", "NOR"};
        for (int i = 0; i < 6; ++i)
            types.addItem(names[i], true, static_cast<int>(gt->type) == i, [edit, id, i] {
                edit("Gate type", [id, i](ProjectDocument& d) {
                    for (auto& g : d.plots.front().gates)
                        if (g.id == id) g.type = static_cast<GateType>(i);
                    return true;
                });
            });
        menu.addSubMenu("Type", types);
        menu.addItem("Delete gate", [edit, id] { edit("Delete gate", [id](ProjectDocument& d) { return editing::deleteGate(d, id); }); });
        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
        return;
    }
    if (const auto* sw = cell ? switchAt(*cell) : nullptr) {
        const auto id = sw->id;
        state_.select({SelectionKind::Switch, id.value()});
        menu.addItem(sw->on ? "Turn off" : "Turn on", [self, id] { if (self != nullptr) self->flipSwitch(id); });
        menu.addItem("Delete Switch", [edit, id] { edit("Delete Switch", [id](ProjectDocument& d) { return editing::deleteSwitch(d, id); }); });
        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
        return;
    }
    if (const auto fp = routeNear(e.position)) {
        const auto id = *fp;
        state_.select({SelectionKind::Flowpoint, id.value()});
        menu.addItem("Flip corner", [edit, id] {
            edit("Flip route", [id](ProjectDocument& d) {
                auto* f = editing::findFlowpoint(d, id);
                f->elbow = f->elbow == RouteElbow::HorizontalFirst ? RouteElbow::VerticalFirst : RouteElbow::HorizontalFirst;
                return true;
            });
        });
        menu.addItem("Delete route", [edit, id] { edit("Delete route", [id](ProjectDocument& d) { return editing::deleteFlowpoint(d, id); }); });
        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
        return;
    }
    if (cell) {
        const auto c = *cell;
        state_.select({});
        menu.addItem("Add Station here", [self, c] { if (self != nullptr) self->addStationAt(c); });
        menu.addItem("Add logic gate here", [edit, c] { edit("Add gate", [c](ProjectDocument& d) { return editing::addGate(d, c, GateType::And).has_value(); }); });
        menu.addItem("Add Switch here", [edit, c] { edit("Add Switch", [c](ProjectDocument& d) { return editing::addSwitch(d, c).has_value(); }); });
        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
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
    if (key == juce::KeyPress::escapeKey && (connectingFrom_ || tool_ != Tool::Select)) {
        setTool(Tool::Select);
        return true;
    }
    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=') return zoom(1), true;
    if (key.getTextCharacter() == '-') return zoom(-1), true;
    return false;
}

} // namespace signalflux::app
