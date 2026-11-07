#include "EffectEditors.h"

namespace Reel
{
using namespace juce;

static const Colour bandColours[EQPlugin::numBands] = { Colour (0xff8ea4e8), Colour (0xff6fb8a6), Colour (0xff9bc46a), Colour (0xffd6c15a),
                                                        Colour (0xffe3a15e), Colour (0xffe07a8a), Colour (0xffc49ae0), Colour (0xff5fc0d6) };
static bool hasGain (int type) { return type == EQPlugin::bell || type == EQPlugin::lowShelf || type == EQPlugin::highShelf; }

//==============================================================================
EQDisplay::EQDisplay (EQPlugin& e, UndoManager& u) : eq (&e), um (u) { startTimerHz (25); }

float EQDisplay::xForHz (double hz) const   { return (float) (getWidth() * std::log (jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)); }
double EQDisplay::hzForX (float x) const    { return 20.0 * std::pow (1000.0, jlimit (0.0, 1.0, (double) x / jmax (1, getWidth()))); }
float EQDisplay::yForDb (double db) const   { return (float) (getHeight() * 0.5 - db / 24.0 * (getHeight() * 0.5 - 12)); }
double EQDisplay::dbForY (float y) const    { return (getHeight() * 0.5 - y) / (getHeight() * 0.5 - 12) * 24.0; }

Point<float> EQDisplay::nodePos (int b) const
{
    auto& band = eq->bands[b];
    return { xForHz (band.freq->getCurrentValue()), yForDb (hasGain (band.type.get()) ? band.gain->getCurrentValue() : 0.0) };
}

int EQDisplay::nodeAt (Point<float> p) const
{
    int best = -1; float bestD = 14.0f;
    for (int b = 0; b < EQPlugin::numBands; ++b) { const float d = nodePos (b).getDistanceFrom (p); if (d < bestD) { bestD = d; best = b; } }
    return best;
}

void EQDisplay::timerCallback()
{
    const int N = 2048;
    std::vector<float> data ((size_t) N * 2, 0.0f);
    int w = eq->scopeWrite.load();
    for (int i = 0; i < N; ++i)
    {
        const int idx = (w - N + i + EQPlugin::scopeSize) % EQPlugin::scopeSize;
        const float win = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (N - 1));
        data[(size_t) i] = eq->scope[idx] * win;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    for (int i = 0; i < 1024; ++i)
    {
        const float db = 20.0f * std::log10 (data[(size_t) i] * 4.0f / N + 1e-7f);
        spectrum[(size_t) i] = db > spectrum[(size_t) i] ? db : spectrum[(size_t) i] - 1.2f;
    }
    repaint();
}

void EQDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::lane); g.fillRoundedRectangle (r, 6.0f);
    g.setFont (FontOptions (10.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForHz (f);
        g.setColour (Col::line2); g.drawVerticalLine ((int) x, 0, r.getBottom());
        g.setColour (Col::muted); g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), (int) x + 3, getHeight() - 15, 34, 12, Justification::left);
    }
    for (int db : { -18, -12, -6, 0, 6, 12, 18 })
    {
        const float y = yForDb (db);
        g.setColour (db == 0 ? Col::line : Col::line2); g.drawHorizontalLine ((int) y, 0, r.getRight());
        if (db != 0) { g.setColour (Col::muted); g.drawText ((db > 0 ? "+" : "") + String (db), 4, (int) y - 13, 30, 12, Justification::left); }
    }
    // spectrum
    const double sr = eq->sampleRate > 0 ? eq->sampleRate : 44100.0;
    Path spec; bool started = false;
    for (int i = 1; i < 1024; ++i)
    {
        const double hz = i * sr / 2048.0; if (hz < 20 || hz > 20000) continue;
        const float x = xForHz (hz), y = jmap (jlimit (-90.0f, 0.0f, spectrum[(size_t) i]), -90.0f, 0.0f, r.getBottom(), r.getHeight() * 0.15f);
        if (! started) { spec.startNewSubPath (x, r.getBottom()); spec.lineTo (x, y); started = true; } else spec.lineTo (x, y);
    }
    if (started) { spec.lineTo (r.getRight(), r.getBottom()); spec.closeSubPath(); g.setColour (Colour (0x30d6e2e7)); g.fillPath (spec); }
    // selected band's own shape
    if (eq->bands[selected].on.get())
    {
        Path b; b.startNewSubPath (0, yForDb (eq->bandResponseDb (selected, hzForX (0))));
        for (float x = 2; x <= r.getRight(); x += 2) b.lineTo (x, yForDb (eq->bandResponseDb (selected, hzForX (x))));
        g.setColour (bandColours[selected].withAlpha (0.45f)); g.strokePath (b, PathStrokeType (1.5f));
    }
    // overall curve
    Path c; c.startNewSubPath (0, yForDb (eq->responseDb (hzForX (0))));
    for (float x = 2; x <= r.getRight(); x += 2) c.lineTo (x, yForDb (eq->responseDb (hzForX (x))));
    Path fill (c); fill.lineTo (r.getRight(), yForDb (0)); fill.lineTo (0, yForDb (0)); fill.closeSubPath();
    g.setColour (Col::acc.withAlpha (0.12f)); g.fillPath (fill);
    g.setColour (Col::acc); g.strokePath (c, PathStrokeType (2.2f));
    // nodes
    g.setFont (FontOptions (11.0f, Font::bold));
    for (int b = 0; b < EQPlugin::numBands; ++b)
    {
        const auto p = nodePos (b); const bool on = eq->bands[b].on.get() != 0;
        auto circle = Rectangle<float> (18, 18).withCentre (p);
        g.setColour (on ? bandColours[b] : Col::panel2); g.fillEllipse (circle);
        g.setColour (b == selected ? Colours::white : bandColours[b].withAlpha (on ? 1.0f : 0.6f)); g.drawEllipse (circle, b == selected ? 2.0f : 1.2f);
        g.setColour (on ? Colour (0xff10181c) : bandColours[b]); g.drawText (String (b + 1), circle, Justification::centred);
    }
}

void EQDisplay::mouseDown (const MouseEvent& e)
{
    const int b = nodeAt (e.position);
    dragging = b >= 0;
    if (b >= 0) { selected = b; um.beginNewTransaction(); if (onBandSelected) onBandSelected (b); repaint(); }
}

void EQDisplay::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    auto& band = eq->bands[selected];
    if (band.on.get() == 0) band.on = 1;
    band.freq->setParameter ((float) jlimit (20.0, 20000.0, hzForX (e.position.x)), sendNotification);
    if (hasGain (band.type.get())) band.gain->setParameter ((float) jlimit (-24.0, 24.0, dbForY (e.position.y)), sendNotification);
    repaint();
}

void EQDisplay::mouseDoubleClick (const MouseEvent& e)
{
    um.beginNewTransaction();
    const int b = nodeAt (e.position);
    if (b >= 0) { eq->bands[b].on = eq->bands[b].on.get() ? 0 : 1; if (onBandSelected) onBandSelected (b); repaint(); return; }
    for (int k : { 2, 3, 4, 5, 1, 6, 0, 7 })
        if (eq->bands[k].on.get() == 0)
        {
            auto& band = eq->bands[k];
            band.on = 1;
            if (k >= 1 && k <= 6 && ! hasGain (band.type.get())) band.type = EQPlugin::bell;
            band.freq->setParameter ((float) hzForX (e.position.x), sendNotification);
            if (hasGain (band.type.get())) band.gain->setParameter ((float) jlimit (-24.0, 24.0, dbForY (e.position.y)), sendNotification);
            selected = k; if (onBandSelected) onBandSelected (k);
            break;
        }
    repaint();
}

void EQDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int b = nodeAt (e.position);
    if (b < 0) { Component::mouseWheelMove (e, w); return; }   // not on a point: scroll the panel
    um.beginNewTransaction();
    auto* q = eq->bands[b].q;
    q->setParameter (jlimit (0.1f, 18.0f, q->getCurrentValue() * std::exp (w.deltaY * 1.2f)), sendNotification);
    repaint();
}

//==============================================================================
void CompressorDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto meter = r.removeFromRight (34).reduced (6, 0);
    const float side = jmin (r.getWidth() - 8, r.getHeight());
    auto plot = Rectangle<float> (r.getX(), r.getY(), side, side);
    g.setColour (Col::lane); g.fillRoundedRectangle (plot, 6.0f);
    auto px = [&] (float db) { return plot.getX() + (db + 60.0f) / 60.0f * plot.getWidth(); };
    auto py = [&] (float db) { return plot.getBottom() - (jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * plot.getHeight(); };
    g.setColour (Col::line2);
    for (int db = -48; db < 0; db += 12) { g.drawVerticalLine ((int) px ((float) db), plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) py ((float) db), plot.getX(), plot.getRight()); }
    g.setColour (Col::line); g.drawLine (px (-60), py (-60), px (0), py (0), 1.0f);
    const float T = comp->threshold->getCurrentValue();
    const float dashes[] = { 4.0f, 4.0f };
    g.setColour (Col::muted); g.drawDashedLine ({ px (T), plot.getY(), px (T), plot.getBottom() }, dashes, 2, 1.0f);
    Path c; c.startNewSubPath (px (-60), py (comp->curveDb (-60)));
    for (float db = -59; db <= 0; db += 0.5f) c.lineTo (px (db), py (comp->curveDb (db)));
    g.setColour (Col::acc); g.strokePath (c, PathStrokeType (2.2f));
    const float lvl = comp->levelDb.load();
    if (lvl > -60.0f) { g.setColour (Colours::white); g.fillEllipse (Rectangle<float> (8, 8).withCentre ({ px (lvl), py (comp->curveDb (lvl)) })); }
    g.setColour (Col::muted); g.setFont (FontOptions (10.5f));
    g.drawText ("in", plot.withTrimmedTop (plot.getHeight() - 14).reduced (4, 0), Justification::right);
    g.drawText ("out", plot.reduced (4, 2), Justification::topLeft);
    // gain reduction meter
    shownGr = jmin (comp->grDb.load(), shownGr * 0.85f);
    g.setColour (Col::lane); g.fillRoundedRectangle (meter.withTrimmedBottom (16), 4.0f);
    const float h = jlimit (0.0f, 1.0f, -shownGr / 24.0f) * (meter.getHeight() - 16);
    g.setColour (Col::acc); g.fillRoundedRectangle (meter.withHeight (h), 4.0f);
    g.setColour (Col::text); g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (String (shownGr, 1), meter.withTop (meter.getBottom() - 14).expanded (6, 0), Justification::centred);
}

void CurveDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Col::lane); g.fillRoundedRectangle (r, 6.0f);
    auto px = [&] (float x) { return r.getX() + (x + 1.0f) * 0.5f * r.getWidth(); };
    auto py = [&] (float y) { return r.getCentreY() - jlimit (-1.3f, 1.3f, y) / 1.3f * (r.getHeight() * 0.5f - 4); };
    g.setColour (Col::line2); g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight()); g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
    g.setColour (Col::line); g.drawLine (px (-1), py (-1), px (1), py (1), 1.0f);
    Path c; bool first = true;
    for (float x = -1.0f; x <= 1.0f; x += 0.01f) { const float y = curve (x); if (first) { c.startNewSubPath (px (x), py (y)); first = false; } else c.lineTo (px (x), py (y)); }
    g.setColour (Col::acc); g.strokePath (c, PathStrokeType (2.2f));
    g.setColour (Col::muted); g.setFont (FontOptions (10.5f)); g.drawText ("shape", r.reduced (6, 4), Justification::topLeft);
}

void EnvelopeDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    g.setColour (Col::lane); g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (6, 8);
    const float a = std::sqrt (jmax (0.001f, A->getCurrentValue())), d = std::sqrt (jmax (0.001f, D->getCurrentValue())), rel = std::sqrt (jmax (0.001f, R->getCurrentValue()));
    float sus = S->getCurrentValue(); if (S->getValueRange().getEnd() > 1.5f) sus /= 100.0f;
    const float hold = 0.6f, total = a + d + hold + rel;
    const float xa = r.getX() + r.getWidth() * a / total, xd = xa + r.getWidth() * d / total, xs = xd + r.getWidth() * hold / total;
    const float ys = r.getBottom() - sus * r.getHeight();
    Path p; p.startNewSubPath (r.getX(), r.getBottom()); p.lineTo (xa, r.getY()); p.quadraticTo (xa + (xd - xa) * 0.3f, ys, xd, ys); p.lineTo (xs, ys); p.quadraticTo (xs + (r.getRight() - xs) * 0.3f, r.getBottom(), r.getRight(), r.getBottom());
    Path f (p); f.closeSubPath();
    g.setColour (colour.withAlpha (0.18f)); g.fillPath (f);
    g.setColour (colour); g.strokePath (p, PathStrokeType (2.0f));
}

//==============================================================================
namespace
{
    /** The controls for the EQ band picked on the display. */
    class EQBandControls : public Component
    {
    public:
        EQBandControls (EQPlugin& e, UndoManager& u, std::function<void()> onChange) : eq (&e), um (u), changed (std::move (onChange)) { setBand (2); }
        void setBand (int b)
        {
            band = b; kids.clear();
            auto& bd = eq->bands[b];
            auto add = [this] (Component* c, int w, int h) { kids.add (c); c->setSize (w, h); addAndMakeVisible (c); };
            auto* lbl = new Label ({}, "Band " + String (b + 1)); lbl->setFont (FontOptions (15.0f, Font::bold)); lbl->setColour (Label::textColourId, bandColours[b]);
            add (lbl, 64, 30);
            auto* on = new ToggleButton ("On"); on->setToggleState (bd.on.get() != 0, dontSendNotification);
            on->onClick = [this, on] { um.beginNewTransaction(); eq->bands[band].on = on->getToggleState() ? 1 : 0; changed(); };
            add (on, 56, 30);
            add (makeChoiceBox (bd.type, { "Low cut", "Low shelf", "Bell", "Notch", "High shelf", "High cut" }, {}, &um, [this] { changed(); Component::SafePointer<EQBandControls> sp (this); MessageManager::callAsync ([sp] { if (sp) sp->setBand (sp->band); }); }), 116, 28);
            if (bd.type.get() == EQPlugin::lowCut || bd.type.get() == EQPlugin::highCut)
                add (makeChoiceBox (bd.slope, { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, {}, &um, [this] { changed(); }), 104, 28);
            add (new ParamKnob (bd.freq, "Frequency", &um), 70, 78);
            if (hasGain (bd.type.get())) add (new ParamKnob (bd.gain, "Gain", &um), 70, 78);
            add (new ParamKnob (bd.q, "Width (Q)", &um), 70, 78);
            add (new ParamKnob (eq->output, "Output", &um), 70, 78);
            resized();
        }
        void resized() override
        {
            int x = 0;
            for (auto* c : kids) { c->setTopLeftPosition (x, c->getHeight() < 40 ? 24 : 0); x += c->getWidth() + 8; }
        }
        int band = 2;
    private:
        ReferenceCountedObjectPtr<EQPlugin> eq; UndoManager& um; std::function<void()> changed;
        OwnedArray<Component> kids;
    };

    void addGenericChoices (Box& box, ReelEffect& fx, UndoManager& um)
    {
        for (auto& c : fx.choices)
        {
            if (c.options.isEmpty()) continue;
            box.add (captionLabel (c.label), GlyphArrangement::getStringWidthInt (Font (FontOptions (12.0f)), c.label) + 10, 28);
            box.add (makeChoiceBox (*c.value, c.options, {}, &um), jmax (96, GlyphArrangement::getStringWidthInt (Font (FontOptions (14.0f)), c.options[0]) + 44), 28);
        }
    }

    void addKnobs (Box& box, te::Plugin& p, UndoManager& um, std::initializer_list<te::AutomatableParameter*> only = {})
    {
        if (only.size() > 0) { for (auto* ap : only) box.add (new ParamKnob (ap, ap->getParameterName(), &um), 72, 78); return; }
        for (auto* ap : p.getAutomatableParameters()) box.add (new ParamKnob (ap, ap->getParameterName(), &um), 72, 78);
    }
}

int buildEffectEditor (Box& box, Session& s, te::Plugin& p)
{
    auto& um = s.undoManager();
    if (auto* eq = dynamic_cast<EQPlugin*> (&p))
    {
        box.breakBefore();
        auto* display = box.add (new EQDisplay (*eq, um), 716, 240);
        box.breakBefore();
        auto* controls = box.add (new EQBandControls (*eq, um, [display] { display->repaint(); }), 716, 80);
        display->onBandSelected = [controls] (int b) { controls->setBand (b); };
        return 744;
    }
    if (auto* tg = dynamic_cast<TranceGatePlugin*> (&p))
    {
        box.breakBefore();
        box.add (new GateRing (*tg, um), 220, 220);
        box.add (new EnvelopeDisplay (tg->attack, tg->decay, tg->sustain, tg->release, Col::acc), 300, 110);
        box.breakBefore();
        addGenericChoices (box, *tg, um);
        addKnobs (box, p, um, { tg->attack, tg->decay, tg->sustain, tg->release, tg->swing, tg->mix });
        return 640;
    }
    if (auto* c = dynamic_cast<CompressorPlugin*> (&p))
    {
        box.breakBefore();
        box.add (new CompressorDisplay (*c), 230, 190);
        addGenericChoices (box, *c, um);
        addKnobs (box, p, um, { c->threshold, c->ratio, c->knee, c->attack, c->release, c->makeup, c->mix, c->scHp });
        return 640;
    }
    if (auto* sat = dynamic_cast<SaturationPlugin*> (&p))
    {
        box.breakBefore();
        box.add (new CurveDisplay (*sat, [sat] (float x) { return sat->curve (x); }), 170, 150);
        addGenericChoices (box, *sat, um);
        addKnobs (box, p, um);
        return 560;
    }
    if (auto* d = dynamic_cast<DistortionPlugin*> (&p))
    {
        box.breakBefore();
        box.add (new CurveDisplay (*d, [d] (float x) { return d->curve (x); }), 170, 150);
        addGenericChoices (box, *d, um);
        addKnobs (box, p, um, { d->drive, d->bias, d->lowCut, d->tone, d->output, d->mix });
        return 560;
    }
    if (auto* v = dynamic_cast<VocoderPlugin*> (&p))
    {
        box.breakBefore();
        addGenericChoices (box, *v, um);
        StringArray notes; Array<int> ids; for (int n = 36; n <= 72; ++n) { notes.add (noteName (n)); ids.add (n); }
        box.add (captionLabel ("Note"), 40, 28);
        box.add (makeChoiceBox (v->noteValue, notes, ids, &um), 80, 28);
        box.breakBefore();
        addKnobs (box, p, um, { v->sharpness, v->attack, v->release, v->formant, v->sibilance, v->breath, v->level, v->mix });
        return 640;
    }
    if (auto* fx = dynamic_cast<ReelEffect*> (&p))
    {
        box.breakBefore();
        addGenericChoices (box, *fx, um);
        box.breakBefore();
        addKnobs (box, p, um);
        return dynamic_cast<PhaserPlugin*> (&p) ? 520 : 640;
    }
    box.breakBefore();
    addKnobs (box, p, um);   // effects saved by older versions
    return 520;
}
}
