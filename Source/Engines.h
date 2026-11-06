#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** An instrument that plays up to four sound layers (recordings, saved instruments or synth sounds) through one of six
    performance engines: Layers, Orbits, Threads, Chords, Disperse and Lattice. */
class EnginePlugin : public ReelEffect, private juce::AsyncUpdater
{
public:
    REEL_EFFECT_NAMES ("reelEngine", "Engine")
    explicit EnginePlugin (te::PluginCreationInfo);
    ~EnginePlugin() override;

    bool isSynth() override                            { return true; }
    bool takesMidiInput() override                     { return true; }
    bool takesAudioInput() override                    { return false; }
    bool producesAudioWhenNoAudioInput() override      { return true; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    enum Type { layersEngine, orbitsEngine, threadsEngine, chordsEngine, disperseEngine, latticeEngine };
    static juce::StringArray engineNames()  { return { "Layers", "Orbits", "Threads", "Chords", "Disperse", "Lattice" }; }
    static juce::StringArray chordNames()   { return { "Major", "Minor", "Sus 2", "Sus 4", "Major 7", "Minor 7", "Ninth", "Fifths", "Cluster" }; }
    static juce::StringArray rateNames()    { return { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" }; }
    static juce::StringArray orderNames()   { return { "Up", "Down", "Ping-pong", "Random", "As played" }; }
    static juce::StringArray motionNames()  { return { "Still", "Circle", "Figure 8", "Spiral" }; }
    static double rateBeats (int c)         { return std::array<double, 6> { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }[(size_t) juce::jlimit (0, 5, c)]; }
    static constexpr int numLayers = 4, numLanes = 3, maxSteps = 16;

    // ---- sounds (message thread) ----
    bool setLayerSound (int layer, const juce::File&, int rootNote, const juce::String& name, juce::UndoManager*);
    void clearLayer (int layer, juce::UndoManager*);
    juce::File layerFile (int) const;
    juce::String layerName (int) const;
    int layerRoot (int) const;
    bool layerLoaded (int) const;

    // ---- Threads lanes: one velocity per step, 0 = rest ----
    std::array<float, maxSteps> laneVelocities (int lane) const;
    void setLaneVelocity (int lane, int step, float v, juce::UndoManager*);

    juce::CachedValue<int> engine, chordType, inversion, motion, divisi, latticeRate, disperseVoices;
    juce::CachedValue<int> laneSteps[numLanes], laneRate[numLanes], laneOrder[numLanes];
    te::AutomatableParameter *level = nullptr, *attack = nullptr, *release = nullptr, *blend = nullptr, *drift = nullptr,
                             *posX = nullptr, *posY = nullptr, *speed = nullptr, *radius = nullptr,
                             *spread = nullptr, *strum = nullptr, *glide = nullptr,
                             *density = nullptr, *jumps = nullptr, *gate = nullptr, *humanize = nullptr, *shift = nullptr;
    te::AutomatableParameter* layerLevel[numLayers] {};
    te::AutomatableParameter* layerPan[numLayers] {};
    te::AutomatableParameter* layerTune[numLayers] {};

    std::atomic<float> shownX { 0.5f }, shownY { 0.5f };          // where the orbit is, for the X/Y pad
    std::atomic<int> shownStep[numLanes] { { -1 }, { -1 }, { -1 } };   // each lane's playhead, for the lane editor

    struct Sound { juce::AudioBuffer<float> data; double rate = 44100; int root = 60; };

private:
    void handleAsyncUpdate() override;
    struct Watcher : juce::ValueTree::Listener
    {
        explicit Watcher (EnginePlugin& o) : owner (o) {}
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { owner.triggerAsyncUpdate(); }
        EnginePlugin& owner;
    } watcher { *this };

    struct Voice
    {
        bool active = false; int layer = 0, key = -1;
        std::shared_ptr<const Sound> snd;
        double pos = 0; float pitch = 60, target = 60, glideStep = 0;
        float vel = 1, gainMul = 1, env = 0; int stage = 0;       // 0 attack, 1 held, 2 release
        int gateLeft = -1, delay = 0;                              // samples until auto release / until it starts
    };
    void startVoice (int layer, float pitch, float vel, int key, float gainMul, int delaySamples = 0, int gateSamples = -1, float fromPitch = -1, float glideSeconds = 0);
    void releaseKey (int key);
    void onNoteOn (int note, float vel);
    void sequencerStep (double beat, int lane);   // lane -1 = Lattice
    float layerWeight (int layer) const;
    void render (juce::AudioBuffer<float>&, int start, int num);

    juce::SpinLock soundLock;
    std::array<std::shared_ptr<const Sound>, numLayers> sounds;
    std::array<juce::String, numLayers> loadedPaths;
    std::array<Voice, 64> voices;
    struct Held { int note; float vel; int order; };
    std::vector<Held> held;
    int orderCounter = 0, laneCount[numLanes] {}, latticeCount = 0;
    double laneLast[numLanes] { -1e9, -1e9, -1e9 }, latticeLast = -1e9;
    double freeBeat = 0, origin = 0, orbitPhase = 0, driftPhase = 0;
    bool wasPlaying = false;
    float orbitW[numLayers] { 1, 0, 0, 0 };
    juce::Random rng;
};
}
