#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** One point on a line. t is in beats from the start of the pattern; pitch is a (fractional) MIDI note. */
struct LinePoint { double t = 0; float pitch = 60, pressure = 0.7f, slide = 0.5f; bool glide = true; };

struct LineData
{
    float velocity = 0.8f, release = 0.5f, vibDepth = 0.0f, vibRate = 5.0f;
    std::vector<LinePoint> pts;   // sorted by time, at least two
    double start() const { return pts.empty() ? 0 : pts.front().t; }
    double end() const   { return pts.empty() ? 0 : pts.back().t; }
    float pitchAt (double t) const;
    float pressureAt (double t) const;
    float slideAt (double t) const;
};

struct LinesPattern
{
    double lengthBeats = 16;
    float transpose = 0, bendRange = 48;
    bool bipolarSlide = false;
    std::vector<LineData> lines;
};

/** Turns a pattern into MPE MIDI: each line gets its own channel (2 to 16) carrying its pitch bend, pressure and slide (CC74). */
class LinesEngine
{
public:
    using Emit = std::function<void (double beat, const juce::MidiMessage&)>;
    void process (const LinesPattern&, double beat0, double beat1, double secondsPerBeat, int steps, const Emit&);
    void allNotesOff (double beat, const Emit&);
    bool isLineSounding (int line) const;
    std::array<bool, 17> channelsInUse() const;
private:
    struct Voice { int line = -1, channel = 0, note = 0, bend = -1, pressure = -1, slide = -1; double cycle = 0; };
    std::vector<Voice> voices;
    int nextChannel = 2;
    double lastEnd = -1.0;
};

namespace LinesIDs
{
    inline const juce::Identifier LINE { "LINE" }, POINT { "POINT" }, t { "t" }, p { "p" }, pr { "pr" }, sl { "sl" }, g { "g" },
                                  v { "v" }, rv { "rv" }, vib { "vib" }, vibr { "vibr" };
}

//==============================================================================
/** The Lines device: draw notes as lines with glides, pressure and slide; plays them as MPE into the track's instrument.
    Regular notes from clips and keyboards pass through to the instrument too. */
class LinesPlugin : public ReelEffect, private juce::AsyncUpdater, private juce::Timer
{
public:
    REEL_EFFECT_NAMES ("reelLines", "Lines")
    explicit LinesPlugin (te::PluginCreationInfo);
    ~LinesPlugin() override;

    bool takesMidiInput() override                    { return true; }
    bool takesAudioInput() override                   { return true; }
    bool producesAudioWhenNoAudioInput() override     { return false; }
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    // ---- editing (message thread) ----
    juce::ValueTree addLine (const LineData&, juce::UndoManager*);
    static LineData lineFromTree (const juce::ValueTree&);
    void writeLine (juce::ValueTree line, const LineData&, juce::UndoManager*);
    juce::Array<juce::ValueTree> lineTrees() const;
    std::shared_ptr<const LinesPattern> snapshot() const;
    double lengthBeats() const;
    static int barsFor (int choice) { return std::array<int, 5> { 1, 2, 4, 8, 16 }[(size_t) juce::jlimit (0, 4, choice)]; }

    juce::CachedValue<int> barsChoice, polarity;
    te::AutomatableParameter* transpose = nullptr;
    std::atomic<bool> recording { false };
    std::atomic<double> playheadBeat { -1.0 };     // for the editor
    int recordedLines = 0;

private:
    void rebuild();
    void handleAsyncUpdate() override { rebuild(); }
    void timerCallback() override;   // turns recorded events into lines
    struct Watcher : juce::ValueTree::Listener
    {
        explicit Watcher (juce::AsyncUpdater& u) : up (u) {}
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { up.triggerAsyncUpdate(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override                 { up.triggerAsyncUpdate(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override          { up.triggerAsyncUpdate(); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                  { up.triggerAsyncUpdate(); }
        juce::AsyncUpdater& up;
    } watcher { *this };

    juce::SpinLock patternLock;
    std::shared_ptr<const LinesPattern> pattern;
    LinesEngine engine;
    te::MPESourceID sourceID;
    int passthroughChannel[128] {};   // regular note -> member channel it was moved to
    int nextPassChannel = 10;

    struct RecEvent { int type, channel, note, value; double beat; };   // 0 on, 1 off, 2 bend, 3 pressure, 4 slide
    juce::AbstractFifo fifo { 1024 };
    std::array<RecEvent, 1024> fifoData;
    struct Open { juce::ValueTree tree; LineData data; int note = 60; double lastBeat = 0, startBeat = 0; float bend = 0; };
    std::map<int, Open> open;   // channel -> line being recorded
};
}
