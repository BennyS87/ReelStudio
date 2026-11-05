#include "LinesEditor.h"

namespace Reel
{
using namespace juce;
static const Colour pressureCol (0xff3a8fd0), slideCol (0xff7ddc8c), lineCol (0xffcdeebf), velCol (0xffd9a441), relCol (0xff9b6fd8);
static bool blackKey (int n) { const int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

LinesCanvas::LinesCanvas (LinesPlugin& p, UndoManager& u) : plugin (&p), um (u) { startTimerHz (30); }

Array<ValueTree> LinesCanvas::targets() const
{
    Array<ValueTree> out;
    for (auto& s : selected) if (s.getParent().isValid()) out.add (s);
    if (out.isEmpty()) out = plugin->lineTrees();
    return out;
}

LinesCanvas::Hit LinesCanvas::hitTest (Point<float> pos) const
{
    auto lines = plugin->lineTrees();
    for (int i = lines.size(); --i >= 0;)
    {
        auto d = LinesPlugin::lineFromTree (lines[i]);
        for (int k = 0; k < (int) d.pts.size(); ++k)
            if (Point<float> (xFor (d.pts[(size_t) k].t), yFor (d.pts[(size_t) k].pitch)).getDistanceFrom (pos) < 8.0f) return { lines[i], k, d.pts[(size_t) k].t };
    }
    for (int i = lines.size(); --i >= 0;)
    {
        auto d = LinesPlugin::lineFromTree (lines[i]);
        if (d.pts.size() < 2) continue;
        const double t = beatAt (pos.x);
        if (t < d.start() || t > d.end()) continue;
        if (std::abs (yFor (d.pitchAt (t)) - pos.y) < 7.0f) return { lines[i], -1, t };
    }
    return {};
}

void LinesCanvas::paint (Graphics& g)
{
    g.fillAll (Col::lane);
    auto lines = plugin->lineTrees();
    if (scrollY < 0)
    {
        float avg = 60, n = 0;
        for (auto& l : lines) { auto d = LinesPlugin::lineFromTree (l); for (auto& p : d.pts) { avg = n == 0 ? p.pitch : avg + p.pitch; ++n; } }
        if (n > 0) avg /= n;
        scrollY = jmax (0.0f, (127.0f - avg) * rowH - getHeight() * 0.5f);
    }
    scrollY = jlimit (0.0f, jmax (0.0f, 128 * rowH - getHeight()), scrollY);
    const int top = jlimit (0, 127, (int) pitchAt (0) + 1), bottom = jlimit (0, 127, (int) pitchAt ((float) getHeight()) - 1);
    for (int p = bottom; p <= top; ++p)
    {
        const float y = yFor ((float) p) - rowH * 0.5f;
        g.setColour (blackKey (p) ? Col::lane2 : Col::lane); g.fillRect ((float) keysW, y, (float) getWidth(), rowH);
        if (p % 12 == 0) { g.setColour (Col::line); g.fillRect ((float) keysW, y + rowH - 1, (float) getWidth(), 1.0f); }
    }
    const double L = plugin->lengthBeats();
    const double sub = grid > 0 && grid * pxPerBeat >= 10 ? grid : 1.0;
    for (double b = std::floor (jmax (0.0, beatAt ((float) keysW))); b <= beatAt ((float) getWidth()); b += sub)
    {
        const bool bar = std::fmod (b + 1e-9, 4.0) < 1e-6, beat = std::fmod (b + 1e-9, 1.0) < 1e-6;
        g.setColour (bar ? Col::line : beat ? Col::line2 : Col::line2.withAlpha (0.45f));
        g.fillRect (xFor (b), 0.0f, 1.0f, (float) getHeight());
        if (bar) { g.setColour (Col::muted); g.setFont (FontOptions (11.0f)); g.drawText (String ((int) (b / 4) + 1), (int) xFor (b) + 3, 2, 30, 12, Justification::left); }
    }
    g.setColour (Colours::black.withAlpha (0.3f)); g.fillRect (Rectangle<float>::leftTopRightBottom (jmax ((float) keysW, xFor (L)), 0, (float) getWidth(), (float) getHeight()));
    g.setColour (Col::acc.withAlpha (0.8f)); g.fillRect (xFor (L) - 1, 0.0f, 2.0f, (float) getHeight());

    auto drawLine = [&] (const LineData& d, bool sel)
    {
        if (d.pts.empty()) return;
        Path line, band, slide;
        std::vector<Point<float>> upper, lower;
        const float x0 = xFor (d.start()), x1 = xFor (jmax (d.end(), d.start() + 1.0 / 64.0));
        bool first = true;
        for (float x = x0; ; x += 2.0f)
        {
            const float xc = jmin (x, x1); const double t = beatAt (xc);
            const float y = yFor (d.pitchAt (t)), half = d.pressureAt (t) * rowH * 1.3f + 1.0f, ys = y + (0.5f - d.slideAt (t)) * rowH * 2.4f;
            if (first) { line.startNewSubPath (xc, y); slide.startNewSubPath (xc, ys); first = false; } else { line.lineTo (xc, y); slide.lineTo (xc, ys); }
            upper.push_back ({ xc, y - half }); lower.push_back ({ xc, y + half });
            if (xc >= x1) break;
        }
        band.startNewSubPath (upper.front()); for (auto& p : upper) band.lineTo (p);
        for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
        band.closeSubPath();
        g.setColour (pressureCol.withAlpha (sel ? 0.5f : 0.32f)); g.fillPath (band);
        g.setColour (slideCol.withAlpha (0.85f)); g.strokePath (slide, PathStrokeType (1.2f));
        g.setColour (sel ? Colours::white : lineCol); g.strokePath (line, PathStrokeType (sel ? 2.4f : 1.8f));
        const float vr = 3.0f + d.velocity * 4.0f;
        g.setColour (velCol); g.fillEllipse (x0 - vr, yFor (d.pts.front().pitch) - vr, vr * 2, vr * 2);
        g.setColour (relCol); g.fillRect (x1 - 1.5f, yFor (d.pts.back().pitch) - rowH * 0.6f, 3.0f, rowH * 1.2f);
        for (auto& p : d.pts)
        {
            auto c = Rectangle<float> (8, 8).withCentre ({ xFor (p.t), yFor (p.pitch) });
            g.setColour (Colour (0xffe8fadf)); g.fillEllipse (c);
            g.setColour (sel ? Col::acc : Colour (0xff4c7a40)); g.drawEllipse (c, 1.2f);
        }
    };
    for (auto& l : lines) drawLine (LinesPlugin::lineFromTree (l), selected.contains (l));
    if (drag == Drag::newLine) drawLine (drawing, true);
    if (! box.isEmpty()) { g.setColour (Col::acc); g.drawRect (box, 1.0f); }
    if (lines.isEmpty() && drag != Drag::newLine)
    {
        g.setColour (Col::muted); g.setFont (FontOptions (15.0f));
        g.drawFittedText ("Press and drag in the grid to draw a line. Glide makes smooth slides between pitches, Step makes jumps.\n"
                          "Each line is a note with its own pitch bend, pressure (blue) and slide (green).", getLocalBounds().withTrimmedLeft (keysW).reduced (30), Justification::centred, 3);
    }
    const double ph = plugin->playheadBeat.load();
    if (ph >= 0) { g.setColour (Col::acc); g.fillRect (xFor (ph) - 1, 0.0f, 2.0f, (float) getHeight()); }
    // keys
    g.setColour (Col::panel); g.fillRect (0, 0, keysW, getHeight());
    for (int p = bottom; p <= top; ++p)
    {
        const float y = yFor ((float) p) - rowH * 0.5f;
        g.setColour (blackKey (p) ? Colour (0xff1c262c) : Colour (0xfff4f7f8)); g.fillRect (0.0f, y, blackKey (p) ? keysW * 0.62f : (float) keysW, rowH - 1);
        if (p % 12 == 0) { g.setColour (Colour (0xff45555d)); g.setFont (FontOptions (9.5f)); g.drawText (noteName (p), 2, (int) y, keysW - 4, (int) rowH, Justification::centredLeft); }
    }
    if (plugin->recording.load()) { g.setColour (Col::rec); g.drawRect (getLocalBounds(), 2); g.setFont (FontOptions (13.0f, Font::bold)); g.drawText ("Recording into Lines", getLocalBounds().reduced (8), Justification::topRight); }
}

void LinesCanvas::mouseDown (const MouseEvent& e)
{
    downPos = e.position; box = {};
    um.beginNewTransaction();
    auto hit = hitTest (e.position);
    if (e.mods.isPopupMenu())
    {
        if (hit.line.isValid())
        {
            auto d = LinesPlugin::lineFromTree (hit.line);
            if (hit.point >= 0 && d.pts.size() > 2) { d.pts.erase (d.pts.begin() + hit.point); plugin->writeLine (hit.line, d, &um); }
            else { selected.removeFirstMatchingValue (hit.line); plugin->state.removeChild (hit.line, &um); }
        }
        repaint(); return;
    }
    if (tool == Tool::draw)
    {
        if (hit.point >= 0) { drag = Drag::point; dragLine = hit.line; dragPoint = hit.point; selected = { hit.line }; }
        else if (hit.line.isValid())
        {
            auto d = LinesPlugin::lineFromTree (hit.line);
            LinePoint np { hit.t, d.pitchAt (hit.t), d.pressureAt (hit.t), d.slideAt (hit.t), true };
            int k = 0; while (k < (int) d.pts.size() && d.pts[(size_t) k].t <= hit.t) ++k;
            if (k > 0) np.glide = d.pts[(size_t) k - 1].glide;
            d.pts.insert (d.pts.begin() + k, np);
            plugin->writeLine (hit.line, d, &um);
            drag = Drag::point; dragLine = hit.line; dragPoint = k; selected = { hit.line };
        }
        else
        {
            const double t = jlimit (0.0, plugin->lengthBeats() - (grid > 0 ? grid : 0.125), snapT (beatAt (e.position.x), e.mods.isAltDown()));
            drawing = {}; drawing.pts.push_back ({ t, (float) roundToInt (pitchAt (e.position.y)), 0.7f, 0.5f, glideMode });
            lastDrawT = t; drag = Drag::newLine;
        }
    }
    else
    {
        if (hit.line.isValid())
        {
            if (e.mods.isShiftDown()) { if (selected.contains (hit.line)) selected.removeFirstMatchingValue (hit.line); else selected.add (hit.line); }
            else if (! selected.contains (hit.line)) selected = { hit.line };
            moveOrig.clear(); for (auto& s : selected) moveOrig.push_back ({ s, LinesPlugin::lineFromTree (s) });
            drag = Drag::moveLines;
        }
        else { if (! e.mods.isShiftDown()) selected.clear(); drag = Drag::box; }
    }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void LinesCanvas::mouseDrag (const MouseEvent& e)
{
    const bool fine = e.mods.isAltDown();
    const double L = plugin->lengthBeats();
    if (drag == Drag::newLine)
    {
        auto& pts = drawing.pts;
        const double t = jlimit (pts.front().t, L, snapT (beatAt (e.position.x), fine));
        const float p = fine ? jlimit (0.0f, 127.0f, pitchAt (e.position.y)) : (float) jlimit (0, 127, roundToInt (pitchAt (e.position.y)));
        if (t > lastDrawT + 1e-9)
        {
            const size_t n = pts.size();
            if (n >= 2 && std::abs (pts[n - 1].pitch - p) < 1e-3f && std::abs (pts[n - 2].pitch - p) < 1e-3f) pts[n - 1].t = t;
            else pts.push_back ({ t, p, 0.7f, 0.5f, glideMode });
            lastDrawT = t;
        }
        else if (pts.size() > 1) pts.back().pitch = p;
        else pts.front().pitch = p;
    }
    else if (drag == Drag::point && dragLine.isValid())
    {
        auto d = LinesPlugin::lineFromTree (dragLine);
        if (dragPoint >= (int) d.pts.size()) return;
        double t = snapT (beatAt (e.position.x), fine);
        const double lo = dragPoint > 0 ? d.pts[(size_t) dragPoint - 1].t + 1.0 / 64.0 : 0.0;
        const double hi = dragPoint + 1 < (int) d.pts.size() ? d.pts[(size_t) dragPoint + 1].t - 1.0 / 64.0 : L;
        auto& p = d.pts[(size_t) dragPoint];
        p.t = jlimit (lo, jmax (lo, hi), t);
        p.pitch = fine ? jlimit (0.0f, 127.0f, pitchAt (e.position.y)) : (float) jlimit (0, 127, roundToInt (pitchAt (e.position.y)));
        plugin->writeLine (dragLine, d, &um);
    }
    else if (drag == Drag::moveLines)
    {
        const double dt = snapT (beatAt (e.position.x) - beatAt (downPos.x), fine);
        const float dp = (float) roundToInt (pitchAt (e.position.y) - pitchAt (downPos.y));
        for (auto& [tree, orig] : moveOrig)
        {
            auto d = orig;
            const double shift = jlimit (-orig.start(), L - orig.end(), dt);
            for (auto& p : d.pts) { p.t += shift; p.pitch = jlimit (0.0f, 127.0f, p.pitch + dp); }
            plugin->writeLine (tree, d, &um);
        }
    }
    else if (drag == Drag::box) box = Rectangle<float> (downPos, e.position);
    repaint();
}

void LinesCanvas::mouseUp (const MouseEvent&)
{
    if (drag == Drag::newLine)
    {
        auto& pts = drawing.pts;
        const double step = grid > 0 ? grid : 0.25;
        if (pts.size() == 1) pts.push_back ({ jmin (plugin->lengthBeats(), pts.front().t + step), pts.front().pitch, 0.7f, 0.5f, glideMode });
        if (pts.back().t <= pts.front().t) pts.back().t = pts.front().t + step;
        // keep the last pitch for a moment so the end of a slide is audible
        auto line = plugin->addLine (drawing, &um);
        selected = { line };
        if (onSelectionChanged) onSelectionChanged();
    }
    if (drag == Drag::box)
    {
        for (auto& l : plugin->lineTrees())
        {
            auto d = LinesPlugin::lineFromTree (l);
            for (auto& p : d.pts) if (box.contains (xFor (p.t), yFor (p.pitch))) { selected.addIfNotAlreadyThere (l); break; }
        }
        box = {};
        if (onSelectionChanged) onSelectionChanged();
    }
    drag = Drag::none; dragLine = {}; dragPoint = -1;
    repaint();
}

void LinesCanvas::mouseDoubleClick (const MouseEvent& e)
{
    auto hit = hitTest (e.position);
    if (! hit.line.isValid()) return;
    um.beginNewTransaction();
    auto d = LinesPlugin::lineFromTree (hit.line);
    if (hit.point >= 0 && d.pts.size() > 2) d.pts.erase (d.pts.begin() + hit.point);
    else if (hit.point < 0)
    {
        int k = 0; while (k + 1 < (int) d.pts.size() && d.pts[(size_t) k + 1].t <= hit.t) ++k;
        d.pts[(size_t) k].glide = ! d.pts[(size_t) k].glide;   // switch this segment between glide and step
    }
    plugin->writeLine (hit.line, d, &um);
    repaint();
}

void LinesCanvas::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { mouseMagnify (e, (float) std::exp (w.deltaY * 1.5)); return; }
    float dx = w.deltaX, dy = w.deltaY; if (e.mods.isShiftDown()) { dx = dy; dy = 0; }
    scrollBeat = jmax (0.0, scrollBeat - dx * 300.0 / pxPerBeat);
    scrollY -= dy * 300.0f;
    repaint();
}

void LinesCanvas::mouseMagnify (const MouseEvent& e, float s)
{
    const double b = beatAt (e.position.x);
    pxPerBeat = jlimit (8.0, 400.0, pxPerBeat * s);
    scrollBeat = jmax (0.0, b - (e.position.x - keysW) / pxPerBeat);
    repaint();
}

void LinesCanvas::deleteSelected()
{
    if (selected.isEmpty()) return;
    um.beginNewTransaction();
    for (auto& s : selected) if (s.getParent().isValid()) plugin->state.removeChild (s, &um);
    selected.clear();
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void LinesCanvas::quantizeSelected()
{
    const double q = grid > 0 ? grid : 0.25;
    um.beginNewTransaction();
    for (auto& l : targets())
    {
        auto d = LinesPlugin::lineFromTree (l);
        std::vector<LinePoint> out;
        for (auto p : d.pts) { p.t = std::round (p.t / q) * q; if (! out.empty() && std::abs (out.back().t - p.t) < 1e-9) out.back() = p; else out.push_back (p); }
        if (out.size() == 1) { auto p = out.front(); p.t += q; out.push_back (p); }
        d.pts = out;
        plugin->writeLine (l, d, &um);
    }
    repaint();
}

//==============================================================================
float ExpressionLane::xFor (double beat) const { return canvas.xFor (beat); }

void ExpressionLane::paint (Graphics& g)
{
    g.fillAll (Col::lane2);
    g.setColour (Col::line); g.drawHorizontalLine (0, 0, (float) getWidth());
    const char* names[] = { "Pressure", "Slide", "Velocity" };
    g.setColour (Col::muted); g.setFont (FontOptions (11.0f)); g.drawFittedText (names[mode], 2, 4, LinesCanvas::keysW - 4, 30, Justification::topLeft, 2);
    const float h = (float) getHeight() - 8;
    auto yFor = [&] (float v) { return 4 + (1.0f - v) * h; };
    for (auto& l : canvas.targets())
    {
        auto d = LinesPlugin::lineFromTree (l);
        if (d.pts.empty()) continue;
        if (mode == 2)
        {
            for (auto [t, v, c] : { std::tuple<double, float, Colour> { d.start(), d.velocity, velCol }, { d.end(), d.release, relCol } })
            {
                const float x = xFor (t); g.setColour (c); g.fillRect (x - 1.5f, yFor (v), 3.0f, h + 4 - yFor (v)); g.fillEllipse (x - 4, yFor (v) - 4, 8, 8);
            }
            continue;
        }
        const auto col = mode == 0 ? pressureCol : slideCol;
        Path p; bool first = true;
        for (auto& pt : d.pts) { const float v = mode == 0 ? pt.pressure : pt.slide; if (first) { p.startNewSubPath (xFor (pt.t), yFor (v)); first = false; } else p.lineTo (xFor (pt.t), yFor (v)); }
        g.setColour (col.withAlpha (0.7f)); g.strokePath (p, PathStrokeType (1.5f));
        for (auto& pt : d.pts)
        {
            const float v = mode == 0 ? pt.pressure : pt.slide, x = xFor (pt.t);
            g.setColour (col); g.fillRect (x - 1.0f, yFor (v), 2.0f, h + 4 - yFor (v)); g.fillEllipse (x - 4, yFor (v) - 4, 8, 8);
        }
    }
}

void ExpressionLane::edit (const MouseEvent& e)
{
    const float v = jlimit (0.0f, 1.0f, 1.0f - (e.position.y - 4) / ((float) getHeight() - 8));
    ValueTree bestLine; int bestPoint = -1; float bestD = 12.0f;
    for (auto& l : canvas.targets())
    {
        auto d = LinesPlugin::lineFromTree (l);
        if (mode == 2)
        {
            for (int k : { 0, 1 }) { const float dist = std::abs (xFor (k == 0 ? d.start() : d.end()) - e.position.x); if (dist < bestD) { bestD = dist; bestLine = l; bestPoint = k; } }
            continue;
        }
        for (int k = 0; k < (int) d.pts.size(); ++k) { const float dist = std::abs (xFor (d.pts[(size_t) k].t) - e.position.x); if (dist < bestD) { bestD = dist; bestLine = l; bestPoint = k; } }
    }
    if (! bestLine.isValid()) return;
    auto d = LinesPlugin::lineFromTree (bestLine);
    if (mode == 2) { if (bestPoint == 0) d.velocity = jmax (0.01f, v); else d.release = v; }
    else if (bestPoint < (int) d.pts.size()) { if (mode == 0) d.pts[(size_t) bestPoint].pressure = v; else d.pts[(size_t) bestPoint].slide = v; }
    plugin->writeLine (bestLine, d, &um);
    repaint(); canvas.repaint();
}

//==============================================================================
LinesPanel::LinesPanel (Session& s) : session (s)
{
    session.addChangeListener (this);
    session.editWatchers.add (this);
    styleHint (hint); addAndMakeVisible (hint);
    rebuild();
}

LinesPanel::~LinesPanel() { session.editWatchers.remove (this); session.removeChangeListener (this); }

void LinesPanel::clear() { controls.clear(); canvas.reset(); lane.reset(); shownVersion = -1; shownLines = nullptr; }

void LinesPanel::changeListenerCallback (ChangeBroadcaster*)
{
    // drawing a line changes the project too, so only rebuild when the track or the device itself changes
    auto* t = session.getSelectedTrack();
    const bool inst = t != nullptr && isInstrument (t);
    auto* lp = inst ? session.getLines (*t) : nullptr;
    if (session.selectedTrack != shownTrack || lp != shownLines || inst != shownInstrument) rebuild();
    else if (canvas) { canvas->repaint(); if (lane) lane->repaint(); }
}

void LinesPanel::rebuild()
{
    const auto keepSel = canvas ? canvas->selected : Array<ValueTree>();
    const auto keepGrid = canvas ? canvas->grid : 0.25;
    const auto keepTool = canvas ? canvas->tool : LinesCanvas::Tool::draw;
    const bool keepGlide = canvas ? canvas->glideMode : true;
    const int keepMode = lane ? lane->mode : 0;
    const bool sameTrack = session.selectedTrack == shownTrack;
    if (canvas) view = canvas->getView();
    if (! sameTrack) view = {};
    clear();
    shownTrack = session.selectedTrack; shownVersion = session.structureVersion;
    auto* t = session.getSelectedTrack();
    shownInstrument = t != nullptr && isInstrument (t);
    shownLines = shownInstrument ? session.getLines (*t) : nullptr;
    hint.setText ({}, dontSendNotification);
    auto add = [this] (Component* c, int w, int h) { controls.add (c); c->setSize (w, h); addAndMakeVisible (c); return c; };
    struct Labelled : Component
    {
        Labelled (const String& text, Component* c, int lw) : child (c) { label.reset (captionLabel (text)); addAndMakeVisible (*label); addAndMakeVisible (*child); labelW = lw; }
        void resized() override { label->setBounds (0, 0, labelW, getHeight()); child->setBounds (labelW, 0, getWidth() - labelW, getHeight()); }
        std::unique_ptr<Label> label; std::unique_ptr<Component> child; int labelW;
    };
    auto addLabelled = [&] (const String& text, Component* c, int lw, int w) { add (new Labelled (text, c, lw), lw + w, 28); return c; };
    if (t == nullptr || ! isInstrument (t))
    {
        hint.setText ("Lines is a sequencer for synth, sampler and drum tracks. Select one of those tracks to draw notes as lines with glides, pressure and slide.", dontSendNotification);
        resized(); return;
    }
    auto* lp = session.getLines (*t);
    auto id = t->itemID;
    if (lp == nullptr)
    {
        hint.setText ("Lines turns each note into a line you draw: it can glide or step between pitches and carries its own pressure and slide, played as MPE so every note bends on its own. "
                      "It loops a pattern of 1 to 16 bars with the song. The track's clips and your keyboard still play as normal.", dontSendNotification);
        add (makeTextButton ("Add Lines to " + t->getName(), [this, id] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.addLines (*t); } }), 220, 32)
            ->setColour (TextButton::buttonColourId, Col::acc.withAlpha (0.35f));
        resized(); return;
    }
    auto& um = session.undoManager();
    canvas = std::make_unique<LinesCanvas> (*lp, um);
    canvas->grid = keepGrid; canvas->tool = keepTool; canvas->glideMode = keepGlide;
    canvas->setView (view);
    for (auto& s : keepSel) if (s.getParent().isValid()) canvas->selected.add (s);
    lane = std::make_unique<ExpressionLane> (*lp, *canvas, um);
    lane->mode = keepMode;
    canvas->onSelectionChanged = [this] { if (lane) lane->repaint(); };
    addAndMakeVisible (*canvas); addAndMakeVisible (*lane);
    auto* c = canvas.get(); auto* ln = lane.get();
    te::Plugin::Ptr keep (lp);

    auto radio = [&] (const String& text, bool on, std::function<void()> fn, int w)
    {
        auto* b = static_cast<TextButton*> (add (makeTextButton (text, fn), w, 28));
        b->setToggleState (on, dontSendNotification);
        b->setColour (TextButton::buttonOnColourId, Col::acc);
        return b;
    };
    auto* bDraw = radio ("Draw", c->tool == LinesCanvas::Tool::draw, nullptr, 60);
    auto* bSel = radio ("Select", c->tool == LinesCanvas::Tool::select, nullptr, 64);
    bDraw->onClick = [c, bDraw, bSel] { c->tool = LinesCanvas::Tool::draw; bDraw->setToggleState (true, dontSendNotification); bSel->setToggleState (false, dontSendNotification); };
    bSel->onClick = [c, bDraw, bSel] { c->tool = LinesCanvas::Tool::select; bSel->setToggleState (true, dontSendNotification); bDraw->setToggleState (false, dontSendNotification); };
    auto* bGl = radio ("Glide", c->glideMode, nullptr, 62);
    auto* bSt = radio ("Step", ! c->glideMode, nullptr, 56);
    bGl->onClick = [c, bGl, bSt] { c->glideMode = true; bGl->setToggleState (true, dontSendNotification); bSt->setToggleState (false, dontSendNotification); };
    bSt->onClick = [c, bGl, bSt] { c->glideMode = false; bSt->setToggleState (true, dontSendNotification); bGl->setToggleState (false, dontSendNotification); };
    auto* gridBox = static_cast<ComboBox*> (addLabelled ("Grid", new ComboBox(), 34, 84));
    const double grids[] = { 1.0, 0.5, 0.25, 0.125, 0.0 }; const char* gridNames[] = { "1/4", "1/8", "1/16", "1/32", "Free" };
    for (int i = 0; i < 5; ++i) { gridBox->addItem (gridNames[i], i + 1); if (std::abs (grids[i] - c->grid) < 1e-9) gridBox->setSelectedId (i + 1, dontSendNotification); }
    gridBox->onChange = [c, gridBox, grids] { c->grid = grids[gridBox->getSelectedId() - 1]; c->repaint(); };
    addLabelled ("Length", makeChoiceBox (lp->barsChoice, { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, {}, &um), 48, 90);
    addLabelled ("Slide", makeChoiceBox (lp->polarity, { "Unipolar", "Bipolar" }, {}, &um), 38, 100);
    auto* tr = static_cast<Slider*> (addLabelled ("Transpose", new Slider (Slider::IncDecButtons, Slider::TextBoxLeft), 68, 96));
    tr->setTextBoxStyle (Slider::TextBoxLeft, false, 44, 26);
    tr->setRange (-24, 24, 1); tr->setValue (lp->transpose->getCurrentValue(), dontSendNotification); tr->setScrollWheelEnabled (false);
    tr->onValueChange = [keep, tr, &um] { if (auto* l = dynamic_cast<LinesPlugin*> (keep.get())) { um.beginNewTransaction(); l->transpose->setParameter ((float) tr->getValue(), sendNotification); } };
    add (makeTextButton ("Quantize", [c] { c->quantizeSelected(); }), 80, 28);
    add (makeTextButton ("Copy", [this, c] { clipboard.clear(); for (auto& s : c->selected) clipboard.push_back (LinesPlugin::lineFromTree (s)); }), 56, 28);
    add (makeTextButton ("Paste", [this, keep, c, &um]
    {
        auto* l = dynamic_cast<LinesPlugin*> (keep.get()); if (l == nullptr || clipboard.empty()) return;
        double first = 1e9, last = 0; for (auto& d : clipboard) { first = jmin (first, d.start()); last = jmax (last, d.end()); }
        const double shift = jmin (last - first, l->lengthBeats() - last);   // paste straight after the copied lines when there's room
        um.beginNewTransaction(); c->selected.clear();
        for (auto d : clipboard) { for (auto& p : d.pts) p.t += shift; c->selected.add (l->addLine (d, &um)); }
        c->repaint();
    }), 56, 28);
    add (makeTextButton ("Delete", [c] { c->deleteSelected(); }), 64, 28);
    add (makeTextButton ("Clear", [keep, c, &um] { if (auto* l = dynamic_cast<LinesPlugin*> (keep.get())) { um.beginNewTransaction(); for (auto& t : l->lineTrees()) l->state.removeChild (t, &um); c->selected.clear(); c->repaint(); } }), 60, 28);
    auto* vib = static_cast<Slider*> (addLabelled ("Vibrato", new Slider (Slider::LinearHorizontal, Slider::TextBoxRight), 52, 150));
    vib->setRange (0.0, 2.0, 0.05); vib->setTextValueSuffix (" st"); vib->setTextBoxStyle (Slider::TextBoxRight, false, 52, 22); vib->setScrollWheelEnabled (false);
    if (! c->selected.isEmpty()) vib->setValue (LinesPlugin::lineFromTree (c->selected[0]).vibDepth, dontSendNotification);
    vib->onDragStart = [&um] { um.beginNewTransaction(); };
    vib->onValueChange = [keep, c, vib, &um] { if (auto* l = dynamic_cast<LinesPlugin*> (keep.get())) for (auto& t : c->targets()) { auto d = LinesPlugin::lineFromTree (t); d.vibDepth = (float) vib->getValue(); l->writeLine (t, d, &um); } };
    auto* rec = static_cast<TextButton*> (add (makeTextButton (lp->recording.load() ? "Stop rec" : "Rec", nullptr), 80, 28));
    rec->setColour (TextButton::buttonColourId, lp->recording.load() ? Col::rec : Col::rec.withAlpha (0.3f));
    rec->onClick = [this, keep, rec]
    {
        auto* l = dynamic_cast<LinesPlugin*> (keep.get()); if (l == nullptr) return;
        const bool on = ! l->recording.load();
        l->recording.store (on);
        session.routeLiveInput();
        if (on && ! session.transport().isPlaying()) session.togglePlay();
        if (! on && session.onMessage) session.onMessage ("Recorded notes are now lines you can reshape.");
        if (on && session.onMessage) session.onMessage ("Play your keyboard: each note becomes a line. Bends, pressure and slide from an MPE controller are kept.");
        rec->setButtonText (on ? "Stop rec" : "Rec"); rec->setColour (TextButton::buttonColourId, on ? Col::rec : Col::rec.withAlpha (0.3f));
    };
    add (makeTextButton ("Zoom -", [c] { c->pxPerBeat = jmax (8.0, c->pxPerBeat / 1.3); c->repaint(); }), 64, 28);
    add (makeTextButton ("Zoom +", [c] { c->pxPerBeat = jmin (400.0, c->pxPerBeat * 1.3); c->repaint(); }), 64, 28);
    auto* p0 = radio ("Pressure", ln->mode == 0, nullptr, 76); auto* p1 = radio ("Slide", ln->mode == 1, nullptr, 56); auto* p2 = radio ("Velocity", ln->mode == 2, nullptr, 72);
    for (auto [b, m] : { std::pair<TextButton*, int> { p0, 0 }, { p1, 1 }, { p2, 2 } })
        b->onClick = [ln, p0, p1, p2, m = m] { ln->mode = m; p0->setToggleState (m == 0, dontSendNotification); p1->setToggleState (m == 1, dontSendNotification); p2->setToggleState (m == 2, dontSendNotification); ln->repaint(); };
    add (makeTextButton ("Remove Lines", [this, id] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.removeLines (*t); } }), 110, 28);
    hint.setText ("Draw: press and drag to draw a line, drag a point to move it, click a line to add a point, double-click a segment to switch glide and step, right-click to delete. "
                  "Select: drag lines to move them, shift-drag to box-select. Alt turns off snapping. The lane below edits pressure, slide or velocity.", dontSendNotification);
    resized();
}

void LinesPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    int x = r.getX(), y = r.getY(), lineH = 0;
    for (auto* c : controls)
    {
        if (x + c->getWidth() > r.getRight() && x > r.getX()) { x = r.getX(); y += lineH + 5; lineH = 0; }
        c->setTopLeftPosition (x, y); x += c->getWidth() + 6; lineH = jmax (lineH, c->getHeight());
    }
    if (! controls.isEmpty()) r.setTop (y + lineH + 6);
    if (canvas == nullptr) { hint.setBounds (r.removeFromTop (60)); return; }
    hint.setBounds (r.removeFromBottom (34));
    lane->setBounds (r.removeFromBottom (jmin (90, r.getHeight() / 3)));
    canvas->setBounds (r);
}
}
