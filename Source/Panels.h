#pragma once
#include "Session.h"
#include "Widgets.h"

namespace Reel
{
/** Base for bottom panels that rebuild when the selected track or the edit's structure changes. */
class RebuildingPanel : public juce::Component, protected juce::ChangeListener, public EditWatcher
{
public:
    explicit RebuildingPanel (Session& s) : session (s) { session.addChangeListener (this); session.editWatchers.add (this); }
    ~RebuildingPanel() override { session.editWatchers.remove (this); session.removeChangeListener (this); }
    void editAboutToBeDeleted() override { items.clear(); breaks.clear(); shownVersion = -1; shownTrack = {}; }
    void paint (juce::Graphics& g) override { g.fillAll (Col::panel); }
    void resized() override { layout(); }
    void rebuildNow() { shownTrack = session.selectedTrack; shownVersion = session.structureVersion; items.clear(); breaks.clear(); build(); for (auto* c : items) addAndMakeVisible (c); layout(); repaint(); if (onContentChanged) onContentChanged(); }
protected:
    virtual void build() = 0;
    virtual void layout();
public:
    int requiredHeight (int width);
    std::function<void()> onContentChanged;
protected:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (session.selectedTrack != shownTrack || session.structureVersion != shownVersion) rebuildNow();
    }
    template <typename T> T* add (T* c, int w, int h) { items.add (c); c->setSize (w, h); return c; }
    juce::Label* heading (const juce::String& text, bool newLine = true);
    juce::Label* hintText (const juce::String& text, int w = 900, int h = 36);
    void lineBreak() { breaks.insert (items.size()); }
    Session& session;
    juce::OwnedArray<juce::Component> items;
    std::set<int> breaks;
    te::EditItemID shownTrack; int shownVersion = -1;
};

class SoundPanel : public RebuildingPanel
{
public:
    explicit SoundPanel (Session& s) : RebuildingPanel (s) { rebuildNow(); }
private:
    void build() override;
    void buildSynth (te::AudioTrack&, te::FourOscPlugin&);
    void buildSampler (te::AudioTrack&);
    void addInstrumentChooser (te::AudioTrack&, bool isSampler);
    void addArpBox (te::AudioTrack&);
    void buildKit (te::AudioTrack&);
    std::unique_ptr<juce::FileChooser> chooser;
};

class EffectsPanel : public RebuildingPanel
{
public:
    explicit EffectsPanel (Session& s) : RebuildingPanel (s) { rebuildNow(); }
private:
    void build() override;
};

class MixerPanel : public RebuildingPanel
{
public:
    explicit MixerPanel (Session& s) : RebuildingPanel (s) { rebuildNow(); }
private:
    void build() override;
    void layout() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (session.structureVersion != shownVersion) rebuildNow(); else repaint();
    }
};
}
