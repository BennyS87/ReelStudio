#pragma once
#include "Common.h"

namespace Reel
{
//==============================================================================
class ReelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ReelLookAndFeel()
    {
        setColourScheme ({ Col::bg, Col::panel, Col::panel2, Col::line, Col::text, Col::panel2, Col::text, Col::acc, Col::bg });
        setColour (juce::Slider::rotarySliderFillColourId, Col::acc);
        setColour (juce::Slider::rotarySliderOutlineColourId, Col::line);
        setColour (juce::Slider::thumbColourId, Col::acc);
        setColour (juce::Slider::trackColourId, Col::acc);
        setColour (juce::Slider::backgroundColourId, Col::lane);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, Col::text);
        setColour (juce::TextButton::buttonColourId, Col::panel2);
        setColour (juce::TextButton::buttonOnColourId, Col::acc);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff241800));
        setColour (juce::TextButton::textColourOffId, Col::text);
        setColour (juce::ComboBox::backgroundColourId, Col::lane);
        setColour (juce::ComboBox::outlineColourId, Col::line);
        setColour (juce::ToggleButton::tickColourId, Col::acc);
        setColour (juce::Label::textColourId, Col::text);
        setColour (juce::TabbedButtonBar::tabTextColourId, Col::muted);
        setColour (juce::TabbedButtonBar::frontTextColourId, Col::text);
        setColour (juce::TabbedComponent::outlineColourId, Col::line);
        setColour (juce::PopupMenu::backgroundColourId, Col::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Col::panel2);
        setColour (juce::TextEditor::backgroundColourId, Col::lane);
        setColour (juce::TextEditor::outlineColourId, Col::line);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
        const auto size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const float stroke = juce::jmax (3.0f, size * 0.11f), radius = size * 0.5f - stroke * 0.5f;
        const auto c = r.getCentre();
        juce::Path track; track.addCentredArc (c.x, c.y, radius, radius, 0, start, end, true);
        g.setColour (Col::line); g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto angle = start + pos * (end - start);
        juce::Path arc; arc.addCentredArc (c.x, c.y, radius, radius, 0, start, angle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Col::panel); g.fillEllipse (r.reduced (stroke * 1.6f));
        g.setColour (Col::text);
        const auto p1 = c.getPointOnCircumference (radius * 0.2f, angle), p2 = c.getPointOnCircumference (radius * 0.75f, angle);
        g.drawLine ({ p1, p2 }, 2.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (14.0f, h * 0.55f)); }
};

//==============================================================================
/** A knob bound to a Tracktion parameter. Follows automation, writes undoable changes. */
class ParamKnob : public juce::Component, private juce::Timer
{
public:
    ParamKnob (te::AutomatableParameter::Ptr p, juce::String label = {}, juce::UndoManager* um = nullptr) : param (std::move (p)), undo (um)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        const auto range = param->getValueRange();
        slider.setRange (range.getStart(), range.getEnd(), 0.0);
        slider.setSkewFactor (param->valueRange.skew);
        slider.setDoubleClickReturnValue (true, param->getCurrentValue());
        slider.setValue (param->getCurrentValue(), juce::dontSendNotification);
        slider.onDragStart = [this] { if (undo) undo->beginNewTransaction(); param->parameterChangeGestureBegin(); };
        slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
        slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), juce::sendNotification); updateText(); };
        name.setText (label.isNotEmpty() ? label : param->getParameterName(), juce::dontSendNotification);
        for (auto* l : { &name, &value }) { l->setJustificationType (juce::Justification::centred); l->setInterceptsMouseClicks (false, false); addAndMakeVisible (*l); }
        name.setColour (juce::Label::textColourId, Col::muted); name.setFont (juce::FontOptions (11.5f));
        value.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (slider);
        updateText();
        startTimerHz (12);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromBottom (14));
        value.setBounds (r.removeFromBottom (15));
        slider.setBounds (r);
    }
    te::AutomatableParameter::Ptr param;

private:
    void updateText()
    {
        auto t = param->getCurrentValueAsString().replace (".000", "").replace ("00 ", " ").replace ("dB", " dB").replace ("  ", " ");
        value.setText (param->hasAutomationPoints() ? t + " A" : t, juce::dontSendNotification);
        value.setColour (juce::Label::textColourId, param->hasAutomationPoints() ? Col::acc : Col::text);
    }
    void timerCallback() override
    {
        if (! slider.isMouseButtonDown() && std::abs (slider.getValue() - param->getCurrentValue()) > 1e-6)
        { slider.setValue (param->getCurrentValue(), juce::dontSendNotification); updateText(); }
    }
    juce::Slider slider; juce::Label name, value; juce::UndoManager* undo = nullptr;
};

/** A knob for a plain value (sampler root, kit tune...) with getter/setter callbacks. */
class ValueKnob : public juce::Component
{
public:
    ValueKnob (juce::String label, double min, double max, double step, double def, std::function<double()> get, std::function<void (double)> set,
               std::function<juce::String (double)> fmt = nullptr, double skew = 1.0)
        : getter (std::move (get)), setter (std::move (set)), formatter (std::move (fmt))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (min, max, step);
        slider.setSkewFactor (skew);
        slider.setValue (getter(), juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, def);
        slider.onValueChange = [this] { setter (slider.getValue()); refresh(); };
        name.setText (label, juce::dontSendNotification);
        for (auto* l : { &name, &value }) { l->setJustificationType (juce::Justification::centred); l->setInterceptsMouseClicks (false, false); addAndMakeVisible (*l); }
        name.setColour (juce::Label::textColourId, Col::muted); name.setFont (juce::FontOptions (11.5f));
        value.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (slider); refresh();
    }
    std::function<void()> onDragStart;
    void resized() override { auto r = getLocalBounds(); name.setBounds (r.removeFromBottom (14)); value.setBounds (r.removeFromBottom (15)); slider.setBounds (r); }
    void refresh() { auto v = slider.getValue(); value.setText (formatter ? formatter (v) : juce::String (v, 2), juce::dontSendNotification); }
    juce::Slider slider;
private:
    std::function<double()> getter; std::function<void (double)> setter; std::function<juce::String (double)> formatter;
    juce::Label name, value;
};

//==============================================================================
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (te::LevelMeterPlugin& p) : plugin (&p) { plugin->measurer.addClient (client); startTimerHz (30); }
    ~LevelMeter() override { plugin->measurer.removeClient (client); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Col::lane); g.fillRoundedRectangle (r, 3.0f);
        auto bar = r.withTop (r.getBottom() - r.getHeight() * level);
        juce::ColourGradient grad (Col::ok, 0, r.getBottom(), Col::rec, 0, r.getY(), false);
        grad.addColour (0.75, Col::acc);
        g.setGradientFill (grad); g.fillRoundedRectangle (bar, 3.0f);
    }
private:
    void timerCallback() override
    {
        float db = -100.0f;
        for (int c = 0; c < 2; ++c) db = juce::jmax (db, client.getAndClearAudioLevel (c).dB);
        const float target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
        level = juce::jmax (target, level - 0.03f);
        repaint();
    }
    juce::ReferenceCountedObjectPtr<te::LevelMeterPlugin> plugin;   // keeps the meter alive even if its track is deleted
    te::LevelMeasurer::Client client; float level = 0;
};

inline void styleHeading (juce::Label& l, float size = 17.0f)
{
    l.setFont (juce::FontOptions (size, juce::Font::bold));
    l.setColour (juce::Label::textColourId, Col::text);
}

inline void styleHint (juce::Label& l)
{
    l.setFont (juce::FontOptions (13.0f));
    l.setColour (juce::Label::textColourId, Col::muted);
    l.setJustificationType (juce::Justification::topLeft);
    l.setMinimumHorizontalScale (1.0f);
}
}
