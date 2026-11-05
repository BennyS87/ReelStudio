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
        setColourScheme ({ Col::bg, Col::panel, Col::panel2, Col::line, Col::text, Col::panel2, juce::Colours::white, Col::acc, juce::Colours::white });
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
        setColour (juce::ComboBox::textColourId, juce::Colours::white);
        setColour (juce::ComboBox::arrowColourId, juce::Colours::white);
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, Col::muted);
        setColour (juce::ToggleButton::textColourId, juce::Colours::white);
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
        slider.setScrollWheelEnabled (false);   // the wheel scrolls the panel instead of turning knobs
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
    std::function<juce::String (float)> formatter;
    void refresh() { updateText(); }

private:
    void updateText()
    {
        auto t = formatter ? formatter (param->getCurrentValue()) : param->getCurrentValueAsString().replace (".000", "").replace ("dB", " dB").replace ("  ", " ");
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
        slider.setScrollWheelEnabled (false);
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

/** Input level meter for a recording device (devices live as long as the engine). */
class InputMeter : public juce::Component, private juce::Timer
{
public:
    explicit InputMeter (te::LevelMeasurer& m) : measurer (m) { measurer.addClient (client); startTimerHz (30); }
    ~InputMeter() override { measurer.removeClient (client); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Col::lane); g.fillRoundedRectangle (r, 4.0f);
        juce::ColourGradient grad (Col::ok, r.getX(), 0, Col::rec, r.getRight(), 0, false); grad.addColour (0.75, Col::acc);
        g.setGradientFill (grad); g.fillRoundedRectangle (r.withWidth (r.getWidth() * level), 4.0f);
        g.setColour (Col::text); g.setFont (juce::FontOptions (11.0f)); g.drawText (peakDb > -90 ? juce::String (peakDb, 1) + " dB" : juce::String ("silent"), r.reduced (6, 0), juce::Justification::centredRight);
    }
private:
    void timerCallback() override
    {
        float db = -100.0f; for (int c = 0; c < 2; ++c) db = juce::jmax (db, client.getAndClearAudioLevel (c).dB);
        level = juce::jmax (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f), level - 0.03f);
        peakDb = juce::jmax (db, peakDb - 0.5f); repaint();
    }
    te::LevelMeasurer& measurer; te::LevelMeasurer::Client client; float level = 0, peakDb = -100.0f;
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

//==============================================================================
/** A titled, rounded section that flows its children left to right. */
class Box : public juce::Component
{
public:
    Box (juce::String t, juce::String d = {}) : title (std::move (t)), desc (std::move (d)) {}
    template <typename T> T* add (T* c, int w, int h) { kids.add (c); c->setSize (w, h); addAndMakeVisible (c); return c; }
    int heightFor (int w)
    {
        int x = 12, y = top(), lineH = 0;
        for (auto* c : kids) { if ((x + c->getWidth() > w - 10 || breaks.count (kids.indexOf (c))) && x > 12) { x = 12; y += lineH + 6; lineH = 0; } x += c->getWidth() + 6; lineH = juce::jmax (lineH, c->getHeight()); }
        return y + lineH + 12;
    }
    void resized() override
    {
        int x = 12, y = top(), lineH = 0;
        for (auto* c : kids) { if ((x + c->getWidth() > getWidth() - 10 || breaks.count (kids.indexOf (c))) && x > 12) { x = 12; y += lineH + 6; lineH = 0; } c->setTopLeftPosition (x, y); x += c->getWidth() + 6; lineH = juce::jmax (lineH, c->getHeight()); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Col::panel2); g.fillRoundedRectangle (r, 9.0f);
        g.setColour (accent.isTransparent() ? Col::line2 : accent.withAlpha (0.6f)); g.drawRoundedRectangle (r, 9.0f, 1.0f);
        g.setColour (dimmed ? Col::muted : Col::text); g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (title, 12, 8, getWidth() - titleRightGap - 12, 20, juce::Justification::left, true);
        if (desc.isNotEmpty()) { g.setColour (Col::muted); g.setFont (juce::FontOptions (12.5f)); g.drawFittedText (desc, 12, 30, getWidth() - 24, 32, juce::Justification::topLeft, 2); }
    }
    int top() const { return desc.isNotEmpty() ? 64 : 34; }
    void breakBefore() { breaks.insert (kids.size()); }
    juce::String title, desc; bool dimmed = false; int titleRightGap = 0; juce::Colour accent;
    juce::OwnedArray<juce::Component> kids;
    std::set<int> breaks;
};

inline juce::Label* captionLabel (const juce::String& s)
{
    auto* l = new juce::Label ({}, s); l->setColour (juce::Label::textColourId, Col::muted); l->setFont (juce::FontOptions (12.0f));
    l->setJustificationType (juce::Justification::centredLeft); return l;
}

inline juce::TextButton* makeTextButton (const juce::String& text, std::function<void()> fn)
{
    auto* b = new juce::TextButton (text); b->onClick = std::move (fn); return b;
}

/** A combo box bound to an int setting. values[i] is stored for names[i]. */
inline juce::ComboBox* makeChoiceBox (juce::CachedValue<int>& v, const juce::StringArray& names, juce::Array<int> values, juce::UndoManager* um, std::function<void()> after = nullptr)
{
    auto* c = new juce::ComboBox();
    if (values.isEmpty()) for (int i = 0; i < names.size(); ++i) values.add (i);
    for (int i = 0; i < names.size(); ++i) c->addItem (names[i], i + 1);
    c->setSelectedId (values.indexOf (v.get()) + 1, juce::dontSendNotification);
    auto* value = &v;
    c->onChange = [c, value, values, um, after] { if (um) um->beginNewTransaction(); *value = values[c->getSelectedId() - 1]; if (after) after(); };
    return c;
}
}
