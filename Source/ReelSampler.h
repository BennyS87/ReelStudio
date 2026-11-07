#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** Reel's sampler. Classic plays one sound across the keyboard (and can loop it), One Shot plays the whole sound
    from any key, Slice cuts the sound at its hits (or evenly) and puts each slice on its own key from C4 up.
    Every note follows its own pitch bend (so Lines can glide it); pressure and slide come from Lines or MPE keyboards. */
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

    enum Mode { classic = 0, oneShotMode = 1, slice = 2 };
    static juce::StringArray modeNames()   { return { "Classic", "One Shot", "Slice" }; }
    static juce::StringArray methodNames() { return { "Transient", "Beat", "Manual" }; }
    static juce::StringArray filterNames() { return { "Off", "Low pass", "High pass", "Band pass" }; }
    static juce::StringArray lfoNames()    { return { "Pitch", "Filter", "Volume", "Pan" }; }
    static constexpr int firstSliceNote = 60;    // slice 1 plays on C4
    static constexpr int maxSlices = 64;

    // ---- slices (start points as fractions of the sound, first is 0) ----
    std::vector<double> getSlices() const;
    void setSlices (std::vector<double>, juce::UndoManager*);
    int autoSlice (juce::UndoManager*);   // by the chosen method; returns how many slices

    juce::CachedValue<juce::String> file, soundName;
    juce::CachedValue<int> root, oneShot, loopMode, sliceMethod, sliceCount, filterType, lfoTarget, reverse;
    juce::CachedValue<bool> mpe;
    te::AutomatableParameter *level = nullptr, *pan = nullptr, *fine = nullptr, *transpose = nullptr,
                             *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr,
                             *cutoff = nullptr, *resonance = nullptr, *pressureAmt = nullptr, *slideAmt = nullptr,
                             *startAt = nullptr, *loopStart = nullptr, *loopEnd = nullptr, *fadeIn = nullptr, *fadeOut = nullptr,
                             *sensitivity = nullptr, *lfoRate = nullptr, *lfoDepth = nullptr;

    struct Sound { juce::AudioBuffer<float> data; double rate = 44100; };
    std::shared_ptr<const Sound> currentSound() const;
    std::shared_ptr<const std::vector<double>> currentSlices() const;
    std::atomic<int> lastSlicePlayed { -1 };   // for the pads

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
    std::shared_ptr<const std::vector<double>> slices;
    juce::String loadedPath;
    juce::MPESynthesiser synth;
    juce::MidiBuffer midiScratch;
    bool zonesAreMpe = false;
};
}
