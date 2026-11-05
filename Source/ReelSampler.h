#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** Plays one recorded sound across the keyboard. Each note follows its own pitch bend (so Lines can glide a recording),
    pressure can change its loudness and slide can sweep a filter. */
class ReelSampler : public ReelEffect, private juce::AsyncUpdater
{
public:
    REEL_EFFECT_NAMES ("reelSampler", "Sampler")
    explicit ReelSampler (te::PluginCreationInfo);
    ~ReelSampler() override;

    bool isSynth() override                            { return true; }
    bool takesMidiInput() override                     { return true; }
    bool takesAudioInput() override                    { return false; }
    bool producesAudioWhenNoAudioInput() override      { return true; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    /** Loads a sound (message thread). The file is referenced, never changed. */
    bool loadFile (const juce::File&, juce::UndoManager*);
    juce::File getFile() const          { return juce::File (file.get()); }
    bool hasSound() const;
    double soundSeconds() const;
    void setMpe (bool on);

    juce::CachedValue<juce::String> file, soundName;
    juce::CachedValue<int> root, oneShot, loopMode;
    juce::CachedValue<bool> mpe;
    te::AutomatableParameter *level = nullptr, *pan = nullptr, *fine = nullptr, *attack = nullptr, *decay = nullptr, *sustain = nullptr,
                             *release = nullptr, *cutoff = nullptr, *pressureAmt = nullptr, *slideAmt = nullptr, *startAt = nullptr,
                             *loopStart = nullptr, *loopEnd = nullptr;

    struct Sound { juce::AudioBuffer<float> data; double rate = 44100; };
    std::shared_ptr<const Sound> currentSound() const;

private:
    void handleAsyncUpdate() override;
    void applyZones();
    struct Watcher : juce::ValueTree::Listener
    {
        explicit Watcher (ReelSampler& o) : owner (o) {}
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override;
        ReelSampler& owner;
    } watcher { *this };

    mutable juce::SpinLock soundLock;
    std::shared_ptr<const Sound> sound;
    juce::String loadedPath;
    juce::MPESynthesiser synth;
    juce::MidiBuffer midiScratch;
    bool zonesAreMpe = false;
};
}
