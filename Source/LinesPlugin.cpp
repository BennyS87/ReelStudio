#include "LinesPlugin.h"

namespace Reel
{
using namespace juce;

//==============================================================================
static int segmentFor (const std::vector<LinePoint>& p, double t)
{
    int k = 0;
    while (k + 1 < (int) p.size() && p[(size_t) k + 1].t <= t) ++k;
    return k;
}

float LineData::pitchAt (double t) const
{
    if (pts.empty()) return 60;
    if (t <= pts.front().t) return pts.front().pitch;
    if (t >= pts.back().t) return pts.back().pitch;
    const int k = segmentFor (pts, t);
    const auto& a = pts[(size_t) k]; const auto& b = pts[(size_t) k + 1];
    if (! a.glide || b.t <= a.t) return a.pitch;
    return a.pitch + (b.pitch - a.pitch) * (float) ((t - a.t) / (b.t - a.t));
}

static float interp (const std::vector<LinePoint>& pts, double t, float LinePoint::* field)
{
    if (pts.empty()) return 0;
    if (t <= pts.front().t) return pts.front().*field;
    if (t >= pts.back().t) return pts.back().*field;
    const int k = segmentFor (pts, t);
    const auto& a = pts[(size_t) k]; const auto& b = pts[(size_t) k + 1];
    if (b.t <= a.t) return a.*field;
    return a.*field + (b.*field - a.*field) * (float) ((t - a.t) / (b.t - a.t));
}

float LineData::pressureAt (double t) const { return interp (pts, t, &LinePoint::pressure); }
float LineData::slideAt (double t) const    { return interp (pts, t, &LinePoint::slide); }

//==============================================================================
std::array<bool, 17> LinesEngine::channelsInUse() const
{
    std::array<bool, 17> used {}; for (auto& v : voices) used[(size_t) v.channel] = true; return used;
}

bool LinesEngine::isLineSounding (int line) const
{
    for (auto& v : voices) if (v.line == line) return true;
    return false;
}

void LinesEngine::allNotesOff (double beat, const Emit& emit)
{
    for (auto& v : voices)
    {
        emit (beat, MidiMessage::noteOff (v.channel, v.note, (uint8) 64));
        emit (beat, MidiMessage::pitchWheel (v.channel, 8192));
    }
    voices.clear();
    lastEnd = -1.0;
}

void LinesEngine::process (const LinesPattern& pat, double b0, double b1, double spb, int steps, const Emit& emit)
{
    if (lastEnd >= 0 && std::abs (b0 - lastEnd) > 1e-4) allNotesOff (b0, emit);   // jumped: start clean
    lastEnd = b1;
    const double L = jmax (0.25, pat.lengthBeats);
    steps = jmax (1, steps);
    for (int s = 0; s < steps; ++s)
    {
        const double b = b0 + (b1 - b0) * s / steps;
        const double cycle = std::floor (b / L), pos = b - cycle * L;
        // end notes that are finished (or whose line vanished)
        for (auto it = voices.begin(); it != voices.end();)
        {
            const bool gone = it->line >= (int) pat.lines.size();
            const bool done = gone || it->cycle != cycle || pos >= pat.lines[(size_t) it->line].end() || pos < pat.lines[(size_t) it->line].start();
            if (done)
            {
                const float rel = gone ? 0.5f : pat.lines[(size_t) it->line].release;
                emit (b, MidiMessage::noteOff (it->channel, it->note, (uint8) jlimit (1, 127, roundToInt (rel * 127))));
                it = voices.erase (it);
            }
            else ++it;
        }
        for (int i = 0; i < (int) pat.lines.size(); ++i)
        {
            const auto& line = pat.lines[(size_t) i];
            if (line.pts.size() < 2 || pos < line.start() || pos >= line.end()) continue;
            Voice* v = nullptr;
            for (auto& x : voices) if (x.line == i) v = &x;
            const double age = pos - line.start();
            float pitch = line.pitchAt (pos) + pat.transpose;
            if (line.vibDepth > 0) pitch += line.vibDepth * (float) std::sin (MathConstants<double>::twoPi * line.vibRate * age * spb) * (float) jmin (1.0, age * 2.0);
            const float sl = line.slideAt (pos);
            const int pressure = jlimit (0, 127, roundToInt (line.pressureAt (pos) * 127));
            const int slide = jlimit (0, 127, roundToInt ((pat.bipolarSlide ? 0.5f + (sl - 0.5f) : sl) * 127));
            if (v == nullptr)
            {
                auto used = channelsInUse();
                int ch = 0;
                for (int k = 0; k < 8; ++k) { const int c = 2 + (nextChannel - 2 + k) % 8; if (! used[(size_t) c]) { ch = c; break; } }
                if (ch == 0)   // all eight busy: take over the oldest
                {
                    ch = voices.front().channel;
                    emit (b, MidiMessage::noteOff (ch, voices.front().note, (uint8) 64));
                    voices.erase (voices.begin());
                }
                nextChannel = 2 + (ch - 2 + 1) % 8;
                Voice nv; nv.line = i; nv.channel = ch; nv.cycle = cycle;
                nv.note = jlimit (0, 127, roundToInt (line.pts.front().pitch + pat.transpose));
                voices.push_back (nv); v = &voices.back();
                const int bend = jlimit (0, 16383, roundToInt (8192.0 + (pitch - v->note) / pat.bendRange * 8192.0));
                emit (b, MidiMessage::pitchWheel (ch, bend)); v->bend = bend;
                emit (b, MidiMessage::channelPressureChange (ch, pressure)); v->pressure = pressure;
                emit (b, MidiMessage::controllerEvent (ch, 74, slide)); v->slide = slide;
                emit (b, MidiMessage::noteOn (ch, v->note, (uint8) jlimit (1, 127, roundToInt (line.velocity * 127))));
                continue;
            }
            const int bend = jlimit (0, 16383, roundToInt (8192.0 + (pitch - v->note) / pat.bendRange * 8192.0));
            if (bend != v->bend) { emit (b, MidiMessage::pitchWheel (v->channel, bend)); v->bend = bend; }
            if (pressure != v->pressure) { emit (b, MidiMessage::channelPressureChange (v->channel, pressure)); v->pressure = pressure; }
            if (slide != v->slide) { emit (b, MidiMessage::controllerEvent (v->channel, 74, slide)); v->slide = slide; }
        }
    }
}

//==============================================================================
LinesPlugin::LinesPlugin (te::PluginCreationInfo info) : ReelEffect (info), sourceID (te::createUniqueMPESourceID())
{
    transpose = addP ("transpose", "Transpose", { -24.0f, 24.0f, 1.0f }, 0.0f, Fmt::st);
    addChoice (barsChoice, "bars", "Pattern length", { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 2);
    addChoice (polarity, "polarity", "Slide", { "Unipolar", "Bipolar" }, 0);
    state.addListener (&watcher);
    rebuild();
    startTimerHz (30);
}

LinesPlugin::~LinesPlugin()
{
    stopTimer();
    cancelPendingUpdate();
    state.removeListener (&watcher);
}

void LinesPlugin::initialise (const te::PluginInitialisationInfo& info) { sampleRate = info.sampleRate; }

void LinesPlugin::reset()
{
    for (auto& c : passthroughChannel) c = 0;
}

double LinesPlugin::lengthBeats() const { return barsFor (barsChoice.get()) * 4.0; }

LineData LinesPlugin::lineFromTree (const ValueTree& v)
{
    LineData d;
    d.velocity = (float) v.getProperty (LinesIDs::v, 0.8f); d.release = (float) v.getProperty (LinesIDs::rv, 0.5f);
    d.vibDepth = (float) v.getProperty (LinesIDs::vib, 0.0f); d.vibRate = (float) v.getProperty (LinesIDs::vibr, 5.0f);
    for (auto c : v)
        if (c.hasType (LinesIDs::POINT))
            d.pts.push_back ({ (double) c.getProperty (LinesIDs::t), (float) c.getProperty (LinesIDs::p, 60.0f), (float) c.getProperty (LinesIDs::pr, 0.7f),
                               (float) c.getProperty (LinesIDs::sl, 0.5f), (bool) c.getProperty (LinesIDs::g, true) });
    std::stable_sort (d.pts.begin(), d.pts.end(), [] (auto& a, auto& b) { return a.t < b.t; });
    return d;
}

void LinesPlugin::writeLine (ValueTree line, const LineData& d, UndoManager* um)
{
    line.setProperty (LinesIDs::v, d.velocity, um); line.setProperty (LinesIDs::rv, d.release, um);
    line.setProperty (LinesIDs::vib, d.vibDepth, um); line.setProperty (LinesIDs::vibr, d.vibRate, um);
    line.removeAllChildren (um);
    for (auto& p : d.pts)
    {
        ValueTree pt (LinesIDs::POINT);
        pt.setProperty (LinesIDs::t, p.t, nullptr); pt.setProperty (LinesIDs::p, p.pitch, nullptr); pt.setProperty (LinesIDs::pr, p.pressure, nullptr);
        pt.setProperty (LinesIDs::sl, p.slide, nullptr); pt.setProperty (LinesIDs::g, p.glide, nullptr);
        line.addChild (pt, -1, um);
    }
}

ValueTree LinesPlugin::addLine (const LineData& d, UndoManager* um)
{
    ValueTree line (LinesIDs::LINE);
    writeLine (line, d, nullptr);
    state.addChild (line, -1, um);
    return line;
}

juce::Array<ValueTree> LinesPlugin::lineTrees() const
{
    juce::Array<ValueTree> out; for (auto c : state) if (c.hasType (LinesIDs::LINE)) out.add (c); return out;
}

void LinesPlugin::rebuild()
{
    auto p = std::make_shared<LinesPattern>();
    p->lengthBeats = lengthBeats();
    p->bipolarSlide = polarity.get() == 1;
    for (auto c : lineTrees())
    {
        if ((bool) c.getProperty ("rec", false)) continue;   // still being recorded: you hear it live
        auto d = lineFromTree (c);
        if (d.pts.size() >= 2) p->lines.push_back (std::move (d));
    }
    const SpinLock::ScopedLockType l (patternLock);
    pattern = std::move (p);
}

std::shared_ptr<const LinesPattern> LinesPlugin::snapshot() const
{
    const SpinLock::ScopedLockType l (patternLock);
    return pattern;
}

void LinesPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    auto* midi = fc.bufferForMidiMessages;
    if (midi == nullptr) return;
    std::shared_ptr<const LinesPattern> pat;
    { const SpinLock::ScopedTryLockType l (patternLock); if (l.isLocked()) pat = pattern; }
    static thread_local std::shared_ptr<const LinesPattern> lastSeen;
    if (pat) lastSeen = pat; else pat = lastSeen;
    const double spb = 60.0 / currentBpm(), bps = 1.0 / spb;
    const double b0 = fc.editTime.getStart().inSeconds() * bps, b1 = fc.editTime.getEnd().inSeconds() * bps;
    const double L = pat ? pat->lengthBeats : 16.0;

    // regular notes (channel 1, from clips and keyboards) move onto member channels so MPE synths still play them
    auto used = engine.channelsInUse();
    for (auto& m : *midi)
    {
        if (recording.load() && fc.isPlaying)
        {
            const double beat = std::fmod (jmax (0.0, b0 + m.getTimeStamp() * bps), L);
            RecEvent e { -1, m.getChannel(), m.isNoteOnOrOff() ? m.getNoteNumber() : 0, 0, beat };
            if (m.isNoteOn()) { e.type = 0; e.value = m.getVelocity(); }
            else if (m.isNoteOff()) e.type = 1;
            else if (m.isPitchWheel()) { e.type = 2; e.value = m.getPitchWheelValue(); }
            else if (m.isChannelPressure()) { e.type = 3; e.value = m.getChannelPressureValue(); }
            else if (m.isAftertouch()) { e.type = 3; e.value = m.getAfterTouchValue(); }
            else if (m.isController() && m.getControllerNumber() == 74) { e.type = 4; e.value = m.getControllerValue(); }
            if (e.type >= 0) { int s1, n1, s2, n2; fifo.prepareToWrite (1, s1, n1, s2, n2); if (n1 > 0) { fifoData[(size_t) s1] = e; fifo.finishedWrite (1); } }
        }
        if (m.getChannel() != 1 || ! m.isNoteOnOrOff()) continue;
        const int note = m.getNoteNumber();
        if (m.isNoteOn())
        {
            int ch = 0;
            for (int k = 0; k < 7; ++k) { const int c = 10 + (nextPassChannel - 10 + k) % 7; if (! used[(size_t) c]) { ch = c; break; } }
            if (ch == 0) ch = nextPassChannel;
            nextPassChannel = 10 + (ch - 10 + 1) % 7;
            passthroughChannel[note] = ch; used[(size_t) ch] = true;
            m.setChannel (ch);
        }
        else if (passthroughChannel[note] != 0) { m.setChannel (passthroughChannel[note]); passthroughChannel[note] = 0; }
    }

    auto emit = [&] (double beat, const MidiMessage& msg) { midi->addMidiMessage (msg, jmax (0.0, (beat - b0) * spb) + fc.midiBufferOffset, sourceID); };
    if (! fc.isPlaying || pat == nullptr) { engine.allNotesOff (b0, emit); playheadBeat.store (-1.0); return; }
    LinesPattern withTranspose = *pat;
    withTranspose.transpose = transpose->getCurrentValue();
    engine.process (withTranspose, b0, b1, spb, jmax (1, fc.bufferNumSamples / 64), emit);
    playheadBeat.store (std::fmod (b1, L));
}

void LinesPlugin::timerCallback()
{
    const double L = lengthBeats();
    auto finish = [&] (int ch, double at)
    {
        auto it = open.find (ch); if (it == open.end()) return;
        auto& o = it->second;
        LinePoint last = o.data.pts.back(); last.t = jmax (o.data.pts.front().t + 1.0 / 32.0, jmin (L, at));
        o.data.pts.push_back (last);
        writeLine (o.tree, o.data, nullptr);
        o.tree.removeProperty ("rec", nullptr);
        ++recordedLines;
        open.erase (it);
    };
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    auto keyFor = [] (int ch, int note) { return ch == 1 ? 1000 + note : ch; };   // keyboards send every note on channel 1
    auto handle = [&] (const RecEvent& e)
    {
        if (e.type == 0)
        {
            const int key = keyFor (e.channel, e.note);
            finish (key, e.beat);
            Open o; o.note = e.note; o.startBeat = o.lastBeat = e.beat;
            o.data.velocity = e.value / 127.0f;
            o.data.pts.push_back ({ e.beat, (float) e.note, 0.7f, 0.5f, true });
            o.tree = ValueTree (LinesIDs::LINE); o.tree.setProperty ("rec", true, nullptr);
            writeLine (o.tree, o.data, nullptr);
            state.addChild (o.tree, -1, &edit.getUndoManager());
            open[key] = std::move (o);
            return;
        }
        if (e.type == 1) { finish (keyFor (e.channel, e.note), e.beat); return; }
        for (auto& [key, o] : open)
        {
            if (e.channel != 1 && key != e.channel) continue;   // member-channel expression belongs to that channel's note
            if (e.beat < o.lastBeat) continue;                   // wrapped around the pattern
            LinePoint p = o.data.pts.back(); p.t = e.beat;
            const float range = e.channel == 1 ? 2.0f : 48.0f;
            if (e.type == 2) { p.pitch = o.note + (e.value - 8192) / 8192.0f * range; p.glide = true; }
            if (e.type == 3) p.pressure = e.value / 127.0f;
            if (e.type == 4) p.slide = e.value / 127.0f;
            if (e.beat - o.lastBeat < 1.0 / 64.0 && o.data.pts.size() > 1) o.data.pts.back() = p; else o.data.pts.push_back (p);
            o.lastBeat = e.beat;
            writeLine (o.tree, o.data, nullptr);
        }
    };
    for (int i = 0; i < n1; ++i) handle (fifoData[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (fifoData[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);
    if (! recording.load()) { std::vector<int> keys; for (auto& [k, o] : open) keys.push_back (k); for (int k : keys) finish (k, open[k].lastBeat + 0.25); }
}
}
