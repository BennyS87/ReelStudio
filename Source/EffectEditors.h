#pragma once
#include "Session.h"
#include "Widgets.h"

namespace Reel
{
/** Builds the body of a device in the Effects tab: displays, choices and knobs for one effect.
    Returns the preferred width for the device box. */
int buildEffectEditor (Box& box, Session&, te::Plugin&);

/** EQ curve with live spectrum. Drag a point to change frequency and gain, scroll on it to change width,
    double-click a point to switch the band on or off, double-click empty space to add a band there. */
class EQDisplay : public juce::Component, private juce::Timer
{
public:
    EQDisplay (EQPlugin&, juce::UndoManager&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void (int)> onBandSelected;
    int selected = 2;
private:
    float xForHz (double hz) const;
    double hzForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float>) const;
    void timerCallback() override;
    juce::ReferenceCountedObjectPtr<EQPlugin> eq;
    juce::UndoManager& um;
    juce::dsp::FFT fft { 11 };
    std::vector<float> spectrum = std::vector<float> (1024, -100.0f);
    bool dragging = false;
};

class CompressorDisplay : public juce::Component, private juce::Timer
{
public:
    explicit CompressorDisplay (CompressorPlugin& c) : comp (&c) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    juce::ReferenceCountedObjectPtr<CompressorPlugin> comp;
    float shownGr = 0;
};

/** Plots a waveshaper's transfer curve (input left to right, output bottom to top). */
class CurveDisplay : public juce::Component, private juce::Timer
{
public:
    CurveDisplay (te::Plugin& p, std::function<float (float)> f) : plugin (&p), curve (std::move (f)) { startTimerHz (12); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    te::Plugin::Ptr plugin;   // keeps the plugin alive while shown
    std::function<float (float)> curve;
};

/** ADSR picture that follows four parameters. */
class EnvelopeDisplay : public juce::Component, private juce::Timer
{
public:
    EnvelopeDisplay (te::AutomatableParameter::Ptr a, te::AutomatableParameter::Ptr d, te::AutomatableParameter::Ptr s, te::AutomatableParameter::Ptr r, juce::Colour c)
        : A (a), D (d), S (s), R (r), colour (c) { startTimerHz (12); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    te::AutomatableParameter::Ptr A, D, S, R; juce::Colour colour;
};
}

#include "TranceGate.h"
namespace Reel
{
/** The gate's steps as a ring: click a segment to switch it on or off, drag up or down on it for its level. */
class GateRing : public juce::Component, private juce::Timer
{
public:
    GateRing (TranceGatePlugin& g, juce::UndoManager& u) : gate (&g), um (u) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6);
        const auto c = r.getCentre(); const float outer = juce::jmin (r.getWidth(), r.getHeight()) / 2, inner = outer * 0.5f;
        const int n = gate->numSteps(), now = gate->currentStep.load();
        for (int i = 0; i < n; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * i / n - juce::MathConstants<float>::halfPi + 0.04f;
            const float a1 = juce::MathConstants<float>::twoPi * (i + 1) / n - juce::MathConstants<float>::halfPi - 0.04f;
            const float lv = gate->stepLevel (i), rOut = inner + (outer - inner) * (0.35f + 0.65f * juce::jmax (lv, 0.0f));
            juce::Path seg; seg.addCentredArc (c.x, c.y, outer, outer, 0, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, true);
            seg.lineTo (c.x + inner * std::cos (a1), c.y + inner * std::sin (a1));
            seg.addCentredArc (c.x, c.y, inner, inner, 0, a1 + juce::MathConstants<float>::halfPi, a0 + juce::MathConstants<float>::halfPi, false);
            seg.closeSubPath();
            g.setColour (Col::lane2); g.fillPath (seg);
            if (lv > 0)
            {
                juce::Path on; on.addCentredArc (c.x, c.y, rOut, rOut, 0, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, true);
                on.lineTo (c.x + inner * std::cos (a1), c.y + inner * std::sin (a1));
                on.addCentredArc (c.x, c.y, inner, inner, 0, a1 + juce::MathConstants<float>::halfPi, a0 + juce::MathConstants<float>::halfPi, false);
                on.closeSubPath();
                g.setColour (i == now ? Col::text : Col::acc.withAlpha (0.45f + 0.55f * lv)); g.fillPath (on);
            }
            else if (i == now) { g.setColour (Col::muted); g.strokePath (seg, juce::PathStrokeType (1.5f)); }
        }
        g.setColour (Col::panel2); g.fillEllipse (juce::Rectangle<float> (inner * 1.7f, inner * 1.7f).withCentre (c));
        g.setColour (Col::text); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (juce::String (n), juce::Rectangle<float> (inner * 1.6f, 26).withCentre (c.translated (0, -8)), juce::Justification::centred);
        g.setColour (Col::acc); g.setFont (juce::FontOptions (12.0f));
        g.drawText (TranceGatePlugin::rateNames()[gate->rate.get()], juce::Rectangle<float> (inner * 1.6f, 16).withCentre (c.translated (0, 14)), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStep = stepAt (e.position);
        if (dragStep < 0) return;
        um.beginNewTransaction();
        startLevel = gate->stepLevel (dragStep) > 0 ? 0.0f : 1.0f;   // a click switches the step
        gate->setStep (dragStep, startLevel, &um);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStep < 0 || e.getDistanceFromDragStart() < 4) return;
        gate->setStep (dragStep, juce::jlimit (0.0f, 1.0f, (startLevel > 0 ? 1.0f : gate->stepLevel (dragStep)) - e.getDistanceFromDragStartY() / 80.0f), &um);
        repaint();
    }
private:
    int stepAt (juce::Point<float> p) const
    {
        auto r = getLocalBounds().toFloat().reduced (6);
        const auto d = p - r.getCentre(); const float outer = juce::jmin (r.getWidth(), r.getHeight()) / 2;
        const float dist = d.getDistanceFromOrigin();
        if (dist < outer * 0.5f || dist > outer) return -1;
        float a = std::atan2 (d.y, d.x) + juce::MathConstants<float>::halfPi;
        if (a < 0) a += juce::MathConstants<float>::twoPi;
        return juce::jlimit (0, gate->numSteps() - 1, (int) (a / juce::MathConstants<float>::twoPi * gate->numSteps()));
    }
    void timerCallback() override { repaint(); }
    juce::ReferenceCountedObjectPtr<TranceGatePlugin> gate;
    juce::UndoManager& um;
    int dragStep = -1; float startLevel = 0;
};
}
