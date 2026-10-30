#pragma once
#include "Common.h"
#include "ReelPlugins.h"
#include "Dsp.h"

namespace Reel
{
juce::StringArray builtInPresetNames();
juce::StringArray userPresetNames();
void applyPreset (te::FourOscPlugin&, const juce::String& name);   // built-in or "user:<name>"
bool saveUserPreset (te::FourOscPlugin&, const juce::String& name);

/** Effect slots in processing order. */
struct EffectInfo { const char* type; const char* title; const char* blurb; int rank; };
const juce::Array<EffectInfo>& effectSlots();

/** Anything holding on to parts of the current Edit (plugins, parameters, thumbnails) must let go
    when this is called, because the Edit is destroyed straight afterwards. */
struct EditWatcher
{
    virtual ~EditWatcher() = default;
    virtual void editAboutToBeDeleted() = 0;
};

/** Owns the Tracktion Edit and every operation the UI performs on it.
    Imported audio is only ever referenced, never modified: speed, pitch, reverse and trims
    are clip settings that Tracktion renders to temporary proxy files when needed. */
class Session : public juce::ChangeBroadcaster,
                private juce::ValueTree::Listener,
                private juce::AsyncUpdater,
                private juce::Timer
{
public:
    explicit Session (te::Engine&);
    ~Session() override;

    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    juce::File projectFile;              // empty until saved by the user
    te::EditItemID selectedTrack, selectedClip, lastInstrumentTrack;
    int structureVersion = 0;            // bumps when tracks/clips/plugins are added or removed
    std::function<void (const juce::String&)> onMessage;   // short user-facing notices
    juce::ListenerList<EditWatcher> editWatchers;

    // ---- project ----
    void newProject();
    bool openProject (const juce::File&);
    bool saveProject (const juce::File&);
    void saveAutosave();
    void loadDemo();
    juce::String getProjectName() const;

    // ---- tracks & selection ----
    juce::Array<te::AudioTrack*> tracks() const              { return te::getAudioTracks (*edit); }
    te::AudioTrack* addTrack (Kind, const juce::String& name = {});
    te::AudioTrack* getSelectedTrack() const;
    te::Clip* getSelectedClip() const;
    void select (te::AudioTrack*, te::Clip* = nullptr);
    void deleteTrack (te::AudioTrack&);
    void duplicateTrack (te::AudioTrack&);
    void moveTrack (te::AudioTrack&, int delta);

    // ---- clips ----
    te::WaveAudioClip* importAudio (const juce::File&, te::AudioTrack* target = nullptr, std::optional<te::TimePosition> at = {});
    int importMidi (const juce::File&);
    te::MidiClip* addMidiClip (te::AudioTrack&, te::TimePosition start, int bars);
    te::StepClip* addStepClip (te::AudioTrack&, te::TimePosition start, int bars);
    void setStepClipBars (te::StepClip&, int bars);
    bool splitAtPlayhead();
    te::Clip* duplicateClip (te::Clip&);
    void deleteClip (te::Clip&);
    void setReversed (te::WaveAudioClip&, bool);
    void setSpeedPitch (te::WaveAudioClip&, double speed, float semitones, bool keepPitch);
    static bool keepsPitch (te::WaveAudioClip& c) { return c.getTimeStretchMode() != te::TimeStretcher::disabled; }
    te::AudioTrack* makeSamplerFromClip (te::WaveAudioClip&);
    void loopClip (te::Clip&);

    // ---- instruments ----
    te::FourOscPlugin* getSynth (te::AudioTrack&) const;
    te::SamplerPlugin* getSampler (te::AudioTrack&) const;
    void setSynthPreset (te::AudioTrack&, const juce::String& name);
    bool loadSamplerSound (te::AudioTrack&, const juce::File&, bool findNote);
    std::optional<PitchResult> findSamplerNote (te::AudioTrack&);
    void setSamplerRoot (te::AudioTrack&, int note);
    void setDrumSound (te::AudioTrack&, int voice, const juce::File&);
    void resetDrumSound (te::AudioTrack&, int voice);
    float getKitValue (te::AudioTrack&, const juce::Identifier& base, int voice, float def) const;
    void setKitValue (te::AudioTrack&, const juce::Identifier& base, int voice, float value);
    void applyKit (te::AudioTrack&);
    void auditionDrum (te::AudioTrack&, int voice);

    // ---- effects ----
    te::Plugin* getEffect (te::AudioTrack&, const juce::String& type) const;
    te::Plugin* ensureEffect (te::AudioTrack&, const juce::String& type);
    bool isEffectOn (te::AudioTrack&, const juce::String& type) const;
    void setEffectOn (te::AudioTrack&, const juce::String& type, bool);
    void setEchoTiming (te::AudioTrack&, double beats);   // 0 = keep current milliseconds

    // ---- automation ----
    struct AutoTarget { juce::String key, label; te::AutomatableParameter* param = nullptr; };
    juce::Array<AutoTarget> automationTargets (te::AudioTrack&) const;
    te::AutomatableParameter* getAutomationParam (te::AudioTrack&) const;
    void showAutomation (te::AudioTrack&, const juce::String& key);

    // ---- transport ----
    te::TransportControl& transport() const      { return edit->getTransport(); }
    void togglePlay();
    void stop (bool rewind);
    void toggleRecord();
    void seek (te::TimePosition);
    double getBpm() const;
    void setBpm (double);
    te::BeatPosition toBeats (te::TimePosition t) const   { return edit->tempoSequence.toBeats (t); }
    te::TimePosition toTime (te::BeatPosition b) const    { return edit->tempoSequence.toTime (b); }
    te::TimeDuration barLength() const;
    te::TimePosition snapToBar (te::TimePosition) const;

    // ---- live playing ----
    void routeLiveInput();
    void keyboardNote (int note, float velocity, bool isOn);
    juce::StringArray midiInputNames() const;

    // ---- export ----
    bool exportMix (const juce::File&);

    juce::UndoManager& undoManager() const { return edit->getUndoManager(); }
    void beginAction() { edit->getUndoManager().beginNewTransaction(); }

private:
    void attachEdit();
    void detachEdit();
    void createDrumKit (te::AudioTrack&);
    te::AudioTrack* trackForNewClip (Kind);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override                 { ++structureVersion; triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override          { ++structureVersion; triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                  { ++structureVersion; triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { dirty = true; sendChangeMessage(); }
    void timerCallback() override;
    std::shared_ptr<te::VirtualMidiInputDevice> keyboardDevice() const;
    bool dirty = false;
    juce::File autosaveFile;
};
}
