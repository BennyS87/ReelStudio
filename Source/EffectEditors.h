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
