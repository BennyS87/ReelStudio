#include "Arpeggiator.h"

namespace Reel
{
using namespace juce;

ArpPlugin::ArpPlugin (te::PluginCreationInfo info) : ReelEffect (info), sourceID (te::createUniqueMPESourceID())
{
    addChoice (mode, "mode", "Pattern", modeNames(), 0);
    addChoice (rate, "rate", "Rate", rateNames(), 3);
    addChoice (octaves, "octaves", "Octaves", { "1", "2", "3", "4" }, 0);
    addChoice (velMode, "velMode", "Velocity", { "As played", "Fixed" }, 0);
    addChoice (hold, "hold", "Hold", { "Off", "On" }, 0);
    gate = addP ("gate", "Gate", { 0.05f, 2.0f }, 0.8f, Fmt::pct);
    swing = addP ("swing", "Swing", { 0.0f, 1.0f }, 0.0f, Fmt::pct);
    level = addP ("level", "Fixed velocity", { 1.0f, 127.0f, 1.0f }, 100.0f, [] (float v) { return String (roundToInt (v)); });
}

void ArpPlugin::reset()
{
    held.clear(); latched.clear(); offs.clear(); stepCount = 0;
}

void ArpPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    auto* midi = fc.bufferForMidiMessages;
    if (midi == nullptr) return;
    const double spb = 60.0 / currentBpm(), bps = 1.0 / spb;
    double b0, b1;
    if (fc.isPlaying)
    {
        b0 = fc.editTime.getStart().inSeconds() * bps; b1 = fc.editTime.getEnd().inSeconds() * bps;
        if (! wasPlaying) origin = 0;   // in time with the song while it plays
    }
    else
    {
        b0 = freeBeat; b1 = b0 + fc.bufferNumSamples / sampleRate * bps;   // its own clock while stopped
    }
    freeBeat = b1;
    wasPlaying = fc.isPlaying;

    auto emit = [&] (double beat, const MidiMessage& m) { midi->addMidiMessage (m, jmax (0.0, (beat - b0) * spb) + fc.midiBufferOffset, sourceID); };

    // take the played notes out: the arpeggiator decides when they sound
    struct Ev { double beat; MidiMessage m; };
    std::vector<Ev> in;
    for (int i = midi->size(); --i >= 0;)
    {
        auto& m = (*midi)[i];
        if (! m.isNoteOnOrOff()) continue;
        in.push_back ({ b0 + jmax (0.0, m.getTimeStamp()) * bps, m });
        midi->remove (i);
    }
    std::stable_sort (in.begin(), in.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    if (midi->isAllNotesOff)
    {
        for (auto& o : offs) emit (b0, MidiMessage::noteOff (o.channel, o.note));
        offs.clear(); held.clear(); latched.clear();
    }

    const double step = rateBeats (rate.get());
    const double swingOffset = step * 0.33 * swing->getCurrentValue();
    const int numOct = octaves.get() + 1;
    auto& active = [&]() -> std::vector<Held>& { return hold.get() == 1 ? latched : held; }();

    auto nextStep = [&] (double from)   // first step time at or after 'from'
    {
        double k = std::floor ((from - origin) / step) - 1;
        for (;; k += 1)
        {
            const double t = origin + k * step + ((((long long) k) & 1) != 0 ? swingOffset : 0.0);
            if (t >= from - 1e-9) return t;
        }
    };

    auto playStep = [&] (double beat)
    {
        auto& set = hold.get() == 1 ? latched : held;
        if (set.empty()) return;
        std::vector<Held> base = set;
        if (mode.get() != 4) std::sort (base.begin(), base.end(), [] (auto& a, auto& b) { return a.note < b.note; });
        else std::sort (base.begin(), base.end(), [] (auto& a, auto& b) { return a.order < b.order; });
        std::vector<Held> toPlay;
        if (mode.get() == 6)   // chords: everything at once, climbing through the octaves
        {
            const int o = stepCount % numOct;
            for (auto h : base) { h.note += 12 * o; toPlay.push_back (h); }
        }
        else
        {
            std::vector<Held> seq;
            for (int o = 0; o < numOct; ++o) for (auto h : base) { h.note += 12 * o; seq.push_back (h); }
            const int n = (int) seq.size();
            int idx = 0;
            switch (mode.get())
            {
                case 1: idx = n - 1 - stepCount % n; break;
                case 2: case 3:
                {
                    const int period = jmax (1, 2 * n - 2);
                    const int i = stepCount % period;
                    idx = n == 1 ? 0 : (i < n ? i : period - i);
                    if (mode.get() == 3) idx = n - 1 - idx;
                    break;
                }
                case 5: idx = rng.nextInt (n); break;
                default: idx = stepCount % n; break;
            }
            toPlay.push_back (seq[(size_t) idx]);
        }
        const double len = jmax (0.01, step * gate->getCurrentValue());
        for (auto& h : toPlay)
        {
            if (h.note > 127) continue;
            for (auto it = offs.begin(); it != offs.end();)   // same note still sounding: end it first
                if (it->note == h.note && it->channel == h.channel) { emit (beat, MidiMessage::noteOff (it->channel, it->note)); it = offs.erase (it); } else ++it;
            const int vel = velMode.get() == 1 ? roundToInt (level->getCurrentValue()) : jlimit (1, 127, roundToInt (h.velocity * 127.0f));
            emit (beat, MidiMessage::noteOn (h.channel, h.note, (uint8) jlimit (1, 127, vel)));
            offs.push_back ({ beat + len, h.note, h.channel });
        }
        ++stepCount;
    };

    size_t ei = 0;
    double t = b0;
    for (int guard = 0; guard < 10000; ++guard)
    {
        const double nextEv = ei < in.size() ? in[ei].beat : 1e300;
        double nextOff = 1e300; for (auto& o : offs) nextOff = jmin (nextOff, o.beat);
        const double nextSt = active.empty() ? 1e300 : nextStep (t);
        const double tn = jmin (nextEv, nextOff, nextSt);
        if (tn >= b1) break;
        const double at = jmax (tn, b0);
        if (nextOff <= tn)
        {
            for (auto it = offs.begin(); it != offs.end();)
                if (it->beat <= tn) { emit (at, MidiMessage::noteOff (it->channel, it->note)); it = offs.erase (it); } else ++it;
            continue;
        }
        if (nextEv <= tn)
        {
            const auto& m = in[ei++].m;
            const int note = m.getNoteNumber(), ch = m.getChannel();
            if (m.isNoteOn())
            {
                const bool wasEmpty = active.empty();
                if (hold.get() == 1 && held.empty()) latched.clear();   // a new chord replaces the held one
                held.push_back ({ note, ch, m.getFloatVelocity(), orderCounter++ });
                if (hold.get() == 1) latched.push_back (held.back());
                if (wasEmpty)
                {
                    stepCount = 0;
                    if (! fc.isPlaying) origin = at;   // stopped: start straight away
                }
            }
            else
                held.erase (std::remove_if (held.begin(), held.end(), [&] (auto& h) { return h.note == note && h.channel == ch; }), held.end());
            t = at;
            continue;
        }
        playStep (at);
        t = at + 1e-7;
    }
}
}
