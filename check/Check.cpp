// PedwarCheck / PedwarSynthCheck: run the plugin layer of PEDWAR (or PEDWAR Synth) without a host.
//
//   1. The list of controls is exactly the one fixed in 0.1.0 (same ids, order, ranges, defaults, menu lists), the
//      same for both plugins, so saved projects and automation keep working for good.
//   2. Saved settings come back exactly (save, load into a fresh plugin); the Preset entry is stored.
//   3. Init resets every control except Pitch Snap, Scale, Key and Quality; every factory preset sets exactly its
//      settings and leaves those four alone; the Preset menu's choice is saved, and a project from before 0.3.0 shows
//      "Custom".
//   4. Audio through processBlock: track input, side chain, MIDI strikes and key tracking, the bus layouts Logic uses,
//      the latency.
//   5. The panel opens at its proportions, every control is there and live, the modulation rings follow the
//      LFOs, the Preset and Quality menus, the snapped Coarse readout.
//      0.3.2 (the gold look): "PEDWAR" fits its title area at every window size (measured, and checked in the drawn
//      pixels); every text on the panel fits its place (names, titles, every knob's value over its whole range, every
//      switch and menu entry); the fixed artwork is stored as a picture, and the 30-times-a-second update (the live
//      areas only) is much lighter than drawing the whole panel.
//   6. Heavy random automation through the parameters stays finite and bounded.
//
// The build workflow runs both programs on every push and fails if any check fails.
// --write-golden writes the reference control list; --snapshot <folder> writes PNGs of the panel.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <cstdio>
#include <random>
#include <vector>

static int checks = 0, failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const char* kWhich = PedwarAudioProcessor::isSynth ? "PEDWAR Synth" : "PEDWAR";
static constexpr double kPi = 3.14159265358979323846;

static std::vector<juce::RangedAudioParameter*> allParams (PedwarAudioProcessor& proc)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base))
            v.push_back (p);
    return v;
}
static float realValue (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
static void setReal (PedwarAudioProcessor& proc, const char* pid, float v)
{
    auto* p = proc.getValueTreeState().getParameter (pid);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// ------------------------------------------------------------------------------------------------ 1. control list

// 4 decimals; a value that rounds to zero is written as 0 (never "-0"), so the list reads the same on every machine.
static juce::String num (float v)
{
    return juce::String (std::fabs (v) < 0.00005f ? 0.0f : v, 4);
}

static juce::StringArray dumpLayout (PedwarAudioProcessor& proc)
{
    juce::StringArray lines;
    int i = 0;
    for (auto* p : allParams (proc))
    {
        const auto& r = p->getNormalisableRange();
        juce::String line;
        line << i++ << " " << p->paramID << " v" << p->getVersionHint() << " | " << p->getName (100)
             << " | range " << num (r.start) << ".." << num (r.end) << " step " << num (r.interval)
             << " | default " << num (p->convertFrom0to1 (p->getDefaultValue()))
             << " | mid " << num (p->convertFrom0to1 (0.5f));
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) line << " | choices " << c->choices.joinIntoString (";");
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr) line << " | switch";
        lines.add (line);
    }
    return lines;
}

static void testControlList (PedwarAudioProcessor& proc, const juce::File& golden, bool writeGolden)
{
    const auto now = dumpLayout (proc);
    if (writeGolden)
    {
        golden.replaceWithText (now.joinIntoString ("\n") + "\n");
        std::printf ("wrote %s (%d controls)\n", golden.getFullPathName().toRawUTF8(), now.size());
        return;
    }
    juce::StringArray ref;
    ref.addLines (golden.loadFileAsString());
    ref.removeEmptyStrings();
    CHECK (ref.size() == 75, "reference list has %d controls", ref.size());
    CHECK (now.size() == ref.size(), "%s has %d controls, the reference %d", kWhich, now.size(), ref.size());
    int same = 0;
    for (int i = 0; i < ref.size() && i < now.size(); ++i)
    {
        if (ref[i] == now[i]) ++same;
        else std::printf ("control %d differs:\n   fixed: %s\n   now:   %s\n", i, ref[i].toRawUTF8(), now[i].toRawUTF8());
    }
    CHECK (same == ref.size(), "%d of %d controls identical to the fixed list", same, ref.size());
    juce::StringArray ids;
    for (auto* p : allParams (proc)) ids.addIfNotAlreadyThere (p->paramID);
    CHECK (ids.size() == now.size(), "ids are unique");
    for (auto& s : ids)
        CHECK (s.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"), "id %s is plain letters", s.toRawUTF8());
    std::printf ("control list: %d controls, %d identical to the fixed list\n", now.size(), same);
}

// ------------------------------------------------------------------------------------------------ 2. / 3. state

static void testState()
{
    PedwarAudioProcessor a;
    std::mt19937 g (7);
    for (auto* p : allParams (a))
        p->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (g));
    juce::MemoryBlock block;
    a.getStateInformation (block);
    CHECK (block.getSize() > 100, "state saved");
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        CHECK (xml != nullptr && xml->getStringAttribute ("pluginVersion") == JucePlugin_VersionString, "version stored");
        CHECK (xml != nullptr && xml->hasAttribute ("preset"), "preset entry stored");
    }
    PedwarAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    auto pa = allParams (a), pb = allParams (b);
    int diff = 0;
    for (size_t i = 0; i < pa.size(); ++i)
        if (std::fabs (realValue (pa[i]) - realValue (pb[i])) > 1.0e-4f * std::max (1.0f, std::fabs (realValue (pa[i]))))
        {
            ++diff;
            std::printf ("  %s: %g, saved %g\n", pa[i]->paramID.toRawUTF8(), realValue (pb[i]), realValue (pa[i]));
        }
    CHECK (diff == 0, "every control restored (%d differ)", diff);

    // Init: back to defaults, except the player's pitch and quality set-up
    b.setCurrentProgram (0);
    int notDefault = 0, playersKept = 0;
    for (size_t i = 0; i < pb.size(); ++i)
    {
        const auto& pid = pb[i]->paramID;
        const bool players = pid == PdParams::pitchSnap || pid == PdParams::pitchScale || pid == PdParams::pitchKey || pid == PdParams::quality;
        if (players) playersKept += std::fabs (pb[i]->getValue() - pa[i]->getValue()) < 1e-6f ? 1 : 0;
        else if (std::fabs (pb[i]->getValue() - pb[i]->getDefaultValue()) > 1e-6f) ++notDefault;
    }
    CHECK (notDefault == 0, "Init: %d controls not at their default", notDefault);
    CHECK (playersKept == 4, "Init leaves Pitch Snap, Scale, Key and Quality alone (%d of 4)", playersKept);
    CHECK (b.getNumPrograms() == 1 + pedwar::presets::kNumFactory && b.getProgramName (0) == "Init", "Preset menu: Init + %d", pedwar::presets::kNumFactory);

    // A state from an unknown future version with an extra entry still loads
    PedwarAudioProcessor c;
    c.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (c.getCurrentProgram() == 0, "preset restored");
}

// ------------------------------------------------------------------------------------------------ 4. audio

struct Rig
{
    PedwarAudioProcessor proc;
    int inCh = 0;
    double sr = 48000.0;
    explicit Rig (bool sidechainOn, double rate = 48000.0) : sr (rate)
    {
        auto layout = proc.getBusesLayout();
        if (PedwarAudioProcessor::isSynth)
            layout.inputBuses.getReference (0) = sidechainOn ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        else
            layout.inputBuses.getReference (1) = sidechainOn ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        CHECK (proc.setBusesLayout (layout), "bus layout accepted");
        inCh = proc.getTotalNumInputChannels();
        proc.setRateAndBufferSizeDetails (sr, 512);
        proc.prepareToPlay (sr, 512);
    }
    // One block: track (main input) and side chain filled by functions of the sample index.
    std::vector<float> run (int n, long start, std::function<float (long)> track, std::function<float (long)> side,
                            juce::MidiBuffer midi = {})
    {
        const int ch = juce::jmax (2, inCh);
        juce::AudioBuffer<float> buf (ch, n);
        buf.clear();
        const int mainIn = PedwarAudioProcessor::isSynth ? 0 : proc.getChannelCountOfBus (true, 0);
        const int sideIn = proc.getChannelCountOfBus (true, PedwarAudioProcessor::isSynth ? 0 : 1);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < mainIn; ++c) buf.setSample (c, i, track ? track (start + i) : 0.0f);
            for (int c = 0; c < sideIn; ++c) buf.setSample (mainIn + c, i, side ? side (start + i) : 0.0f);
        }
        proc.processBlock (buf, midi);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i) out[(size_t) i] = buf.getSample (0, i);
        return out;
    }
    std::vector<float> runFor (double seconds, std::function<float (long)> track, std::function<float (long)> side, long& pos)
    {
        std::vector<float> all;
        const long total = (long) (seconds * sr);
        while ((long) all.size() < total)
        {
            auto b = run (512, pos, track, side);
            pos += 512;
            all.insert (all.end(), b.begin(), b.end());
        }
        return all;
    }
};

// ------------------------------------------------------------------------------------------------ 3b. presets

static bool sameParams (const pedwar::Params& a, const pedwar::Params& b, bool withPlayers, juce::String& what)
{
    auto near = [] (float x, float y) { return std::fabs (x - y) <= 1.0e-3f * std::max (1.0f, std::fabs (x)); };
    for (int f = 0; f < 4; ++f)
    {
        const auto& x = a.f[f]; const auto& y = b.f[f];
        if (x.mode != y.mode || ! near (x.gainDb, y.gainDb) || x.inLimit != y.inLimit || ! near (x.coarseHz, y.coarseHz)
            || ! near (x.fineCents, y.fineCents) || ! near (x.q, y.q) || x.outLimit != y.outLimit || x.keyTrack != y.keyTrack
            || x.tap != y.tap || x.strike != y.strike || ! near (x.fmAmt, y.fmAmt) || ! near (x.morph, y.morph)
            || x.morphEnds != y.morphEnds || ! near (x.width, y.width) || ! near (x.levelDb, y.levelDb))
        { what = "filter " + juce::String (f); return false; }
    }
    if (a.masterLimit != b.masterLimit || a.linkAB != b.linkAB || a.linkGD != b.linkGD || ! near (a.offset, b.offset)
        || a.pitchSplit != b.pitchSplit || ! near (a.lfoRate, b.lfoRate) || a.stereoMode != b.stereoMode)
    { what = "master"; return false; }
    for (int j = 0; j < 4; ++j) if (a.bus[j] != b.bus[j]) { what = "bus"; return false; }
    if (withPlayers && (a.snap != b.snap || a.scale != b.scale || a.key != b.key || a.quality != b.quality)) { what = "players"; return false; }
    return true;
}

static void testPresets()
{
    using namespace PdParams;
    PedwarAudioProcessor p;
    juce::String what;
    // Init = the engine's own list of defaults
    p.setCurrentProgram (0);
    CHECK (sameParams (p.readParams(), pedwar::presets::init(), true, what), "Init = presets::init() (%s)", what.toRawUTF8());
    // every preset: exactly its settings, the player's Pitch Snap, Scale, Key and Quality untouched
    for (int i = 1; i < p.getNumPrograms(); ++i)
    {
        setReal (p, pitchSnap, (float) (i % 3)); setReal (p, pitchScale, (float) (i * 2 % 29));
        setReal (p, pitchKey, (float) (i % 12)); setReal (p, quality, (float) ((i + 1) % 3));
        const auto before = p.readParams();
        p.setCurrentProgram (i);
        const auto now = p.readParams();
        CHECK (sameParams (now, pedwar::presets::factory (i - 1), false, what), "preset %d sets its settings (%s)", i, what.toRawUTF8());
        CHECK (now.snap == before.snap && now.scale == before.scale && now.key == before.key && now.quality == before.quality,
               "preset %d leaves Pitch Snap, Scale, Key and Quality alone", i);
        CHECK (p.getProgramName (i) == juce::String (pedwar::presets::name (i - 1)) && p.getProgramName (i).isNotEmpty(), "preset %d name", i);
        CHECK (p.getCurrentProgram() == i && p.shownPreset() == i, "preset %d shown", i);
    }
    // the choice is saved with the project and comes back (even after a knob has moved)
    p.setCurrentProgram (5);
    setReal (p, id (0, Q), 7.0f);
    juce::MemoryBlock block;
    p.getStateInformation (block);
    PedwarAudioProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (q.shownPreset() == 5 && q.getCurrentProgram() == 5, "preset choice restored (%d)", q.shownPreset());
    CHECK (std::fabs (realValue (q.getValueTreeState().getParameter (id (0, Q))) - 7.0f) < 1e-3f, "moved knob restored");
    // a project saved by 0.2.0 (always "preset 0", whatever the settings): shown as Custom, settings unchanged
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        xml->setAttribute ("pluginVersion", "0.2.0");
        xml->setAttribute ("preset", 0);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        PedwarAudioProcessor r;
        r.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (r.shownPreset() == -1, "0.2.0 project: Custom (%d)", r.shownPreset());
        CHECK (sameParams (r.readParams(), q.readParams(), true, what), "0.2.0 project: settings exactly as saved (%s)", what.toRawUTF8());
        juce::MemoryBlock again;
        r.getStateInformation (again);
        PedwarAudioProcessor s2;
        s2.setStateInformation (again.getData(), (int) again.getSize());
        CHECK (s2.shownPreset() == -1, "Custom stays Custom after saving again");
    }
    // every preset plays through processBlock (pink-ish noise in, middle C)
    for (int i = 1; i < p.getNumPrograms(); ++i)
    {
        Rig r (true);
        r.proc.setCurrentProgram (i);
        long pos = 0;
        std::mt19937 g (3);
        std::uniform_real_distribution<float> u (-0.2f, 0.2f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        auto o = r.run (512, pos, [&] (long) { return u (g); }, [&] (long) { return u (g); }, midi);
        pos += 512;
        auto rest = r.runFor (1.0, [&] (long) { return u (g); }, [&] (long) { return u (g); }, pos);
        float pk = 0.0f; double ms = 0.0; bool fin = true;
        for (float x : rest) { fin = fin && std::isfinite (x); pk = std::max (pk, std::fabs (x)); ms += (double) x * x; }
        CHECK (fin && pk < 1.5f && ms > 1e-3, "preset %d (%s) plays through the plugin (peak %.2f)", i, p.getProgramName (i).toRawUTF8(), pk);
    }
}

static double toneAmp (const std::vector<float>& v, double hz, double sr, size_t from)
{
    const size_t n = v.size() - from;
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));
        re += v[from + i] * win * std::cos (2.0 * kPi * hz * (double) i / sr);
        im += v[from + i] * win * std::sin (2.0 * kPi * hz * (double) i / sr);
    }
    return 2.0 * std::sqrt (re * re + im * im) / ((double) n * 0.5);
}

static void soloWire (PedwarAudioProcessor& p, int f)
{
    using namespace PdParams;
    for (int k = 0; k < numFilters; ++k) setReal (p, id (k, Level), k == f ? 0.0f : -60.0f);
    setReal (p, id (f, Coarse), 16.0f);
    setReal (p, id (f, Q), 0.5f);
    setReal (p, id (f, Tap), 3.0f);    // HP at 16 Hz: a near-straight wire above 100 Hz
}

static void testAudio()
{
    using namespace PdParams;
    auto sine = [] (double hz, double amp) { return [=] (long n) { return (float) (amp * std::sin (2.0 * kPi * hz * (double) n / 48000.0)); }; };

    // latency: up + down filters at 4x, about 38.5 samples at 48 kHz
    {
        Rig r (true);
        CHECK (r.proc.getLatencySamples() >= 30 && r.proc.getLatencySamples() <= 50, "latency %d samples", r.proc.getLatencySamples());
        CHECK (r.proc.acceptsMidi(), "takes MIDI");
    }

    if (! PedwarAudioProcessor::isSynth)
    {
        // track audio reaches alpha; side chain reaches gamma (and not alpha)
        Rig r (true);
        soloWire (r.proc, 0);
        long pos = 0;
        auto o = r.runFor (0.6, sine (1000, 0.1), sine (3000, 0.1), pos);
        CHECK (std::fabs (toneAmp (o, 1000, 48000, 9600) / 0.1 - 1.0) < 0.06, "track reaches alpha (%.3f)", toneAmp (o, 1000, 48000, 9600) / 0.1);
        CHECK (toneAmp (o, 3000, 48000, 9600) < 0.002, "side chain does not reach alpha");
        Rig s (true);
        soloWire (s.proc, 2);
        pos = 0;
        o = s.runFor (0.6, sine (1000, 0.1), sine (3000, 0.1), pos);
        CHECK (std::fabs (toneAmp (o, 3000, 48000, 9600) / 0.1 - 1.0) < 0.06, "side chain reaches gamma");
        CHECK (toneAmp (o, 1000, 48000, 9600) < 0.002, "track does not reach gamma while the side chain plays");
        // instrument slot of a Software Instrument track (Logic's MIDI-controlled effect set-up): the main input is
        // silent and the chosen track arrives on the side chain, so alpha (and all four) hear it
        Rig m (true);
        soloWire (m.proc, 0);
        pos = 0;
        o = m.runFor (0.6, nullptr, sine (3000, 0.1), pos);
        CHECK (std::fabs (toneAmp (o, 3000, 48000, 9600) / 0.1 - 1.0) < 0.06, "instrument slot: side chain reaches alpha");
        for (int k = 0; k < PdParams::numFilters; ++k)
            CHECK (m.proc.getEngine().filterUsesSidechain (k), "instrument slot: filter %d on the side chain", k);
        // no side chain bus at all: gamma takes the track
        Rig n (false);
        soloWire (n.proc, 3);
        pos = 0;
        o = n.runFor (0.6, sine (1000, 0.1), nullptr, pos);
        CHECK (std::fabs (toneAmp (o, 1000, 48000, 9600) / 0.1 - 1.0) < 0.06, "no side chain: delta takes the track");
        // mono track in, stereo out
        auto layout = n.proc.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        CHECK (n.proc.checkBusesLayoutSupported (layout), "mono in / stereo out supported");
    }
    else
    {
        // PEDWAR Synth: the side chain feeds all four; without it, an Osc filter still sounds
        Rig r (true);
        soloWire (r.proc, 0);
        long pos = 0;
        auto o = r.runFor (0.6, nullptr, sine (3000, 0.1), pos);
        CHECK (std::fabs (toneAmp (o, 3000, 48000, 9600) / 0.1 - 1.0) < 0.06, "synth: side chain reaches alpha");
        Rig q (false);
        for (int k = 0; k < numFilters; ++k) setReal (q.proc, id (k, Level), k == 1 ? 0.0f : -60.0f);
        setReal (q.proc, id (1, Mode), 2.0f);
        setReal (q.proc, id (1, Tap), 1.0f);
        setReal (q.proc, id (1, Coarse), 261.6256f);
        pos = 0;
        q.runFor (0.5, nullptr, nullptr, pos);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
        q.run (512, pos, nullptr, nullptr, midi);
        pos += 512;
        o = q.runFor (1.0, nullptr, nullptr, pos);
        CHECK (toneAmp (o, 440.0, 48000, 9600) > 0.4, "synth: Osc mode plays A4 from the keyboard (%.3f)", toneAmp (o, 440.0, 48000, 9600));
    }

    // MIDI strike through processBlock: the light counter moves, the filter rings
    {
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) setReal (r.proc, id (k, Level), k == 0 ? 0.0f : -60.0f);
        setReal (r.proc, id (0, Coarse), 800.0f);
        setReal (r.proc, id (0, Q), 30.0f);
        setReal (r.proc, id (0, Tap), 1.0f);
        setReal (r.proc, id (2, Strike), 0.0f);
        long pos = 0;
        r.runFor (0.2, nullptr, nullptr, pos);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 100);
        auto o = r.run (512, pos, nullptr, nullptr, midi);
        pos += 512;
        auto rest = r.runFor (0.3, nullptr, nullptr, pos);
        o.insert (o.end(), rest.begin(), rest.end());
        float pk = 0.0f;
        for (float x : o) pk = std::max (pk, std::fabs (x));
        CHECK (pk > 0.4f && pk < 1.2f, "strike rings (peak %.3f)", pk);
        CHECK (r.proc.getEngine().strikeCount (0) == 1 && r.proc.getEngine().strikeCount (2) == 0, "strike on / off per filter");
    }
}

// ------------------------------------------------------------------------------------------------ 4b. milestone 2

static void testMilestone2()
{
    using namespace PdParams;
    // Width: 0 -> both sides the same; 100% -> alpha's right side differs (the all-pass copy). The sound is alpha
    // oscillating (Osc mode), so the check is the same in both plugins.
    {
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) setReal (r.proc, id (k, Level), k == 0 ? 0.0f : -60.0f);
        setReal (r.proc, id (0, Mode), 2.0f);
        setReal (r.proc, id (0, Coarse), 300.0f);
        setReal (r.proc, id (0, Q), 0.7071f);
        setReal (r.proc, id (0, Morph), 0.0f);       // fully left: low-pass
        setReal (r.proc, id (0, Width), 0.0f);
        long pos = 0;
        double diff0 = 0.0, diff1 = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            const int ch = juce::jmax (2, r.inCh);
            juce::AudioBuffer<float> buf (ch, 512);
            buf.clear();
            juce::MidiBuffer midi;
            r.proc.processBlock (buf, midi);
            pos += 512;
            if (b == 20) setReal (r.proc, id (0, Width), 1.0f);
            if (b > 10 && b < 20) for (int i = 0; i < 512; ++i) diff0 = std::max (diff0, (double) std::fabs (buf.getSample (0, i) - buf.getSample (1, i)));
            if (b > 30) for (int i = 0; i < 512; ++i) diff1 = std::max (diff1, (double) std::fabs (buf.getSample (0, i) - buf.getSample (1, i)));
        }
        CHECK (diff0 < 1e-5, "Width 0: left = right (%g)", diff0);
        CHECK (diff1 > 0.02, "Width 100%%: right side is the all-pass copy (%g)", diff1);
    }
    // Link, Offset, Pitch split and the bus grid reach the engine through the controls
    {
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) { setReal (r.proc, id (k, KeyTrack), 0.0f); setReal (r.proc, id (k, Coarse), 220.0f); }
        auto settle = [&r] { long pos = 0; r.runFor (0.2, nullptr, nullptr, pos); };
        settle();
        CHECK (std::fabs (r.proc.getEngine().playedHz (1) - 220.0f) < 0.5f, "beta at its own 220 Hz");
        setReal (r.proc, linkAB, 1.0f);              // alpha is one octave above its default 110 Hz
        settle();
        CHECK (std::fabs (r.proc.getEngine().playedHz (1) - 440.0f) < 0.5f, "Link: beta follows alpha (%.1f Hz)", r.proc.getEngine().playedHz (1));
        setReal (r.proc, linkAB, 0.0f);
        setReal (r.proc, offset, 0.5f);              // +6 semitones
        settle();
        CHECK (std::fabs (r.proc.getEngine().playedHz (3) - 311.127f) < 0.5f, "Offset +6 st (%.1f Hz)", r.proc.getEngine().playedHz (3));
        setReal (r.proc, offset, 0.0f);
        for (int k = 0; k < numFilters; ++k) setReal (r.proc, id (k, KeyTrack), 1.0f);
        setReal (r.proc, pitchSplit, 1.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        r.run (512, 0, nullptr, nullptr, midi);
        settle();
        CHECK (std::fabs (r.proc.getEngine().playedHz (2) - 110.0f) < 0.5f && std::fabs (r.proc.getEngine().playedHz (0) - 220.0f) < 0.5f,
               "Pitch split: C3 moves gamma (%.1f Hz), not alpha (%.1f Hz)", r.proc.getEngine().playedHz (2), r.proc.getEngine().playedHz (0));
        setReal (r.proc, busIds[1], 1.0f);           // beta's LFO on FM: moves alpha
        setReal (r.proc, id (0, FmAmt), -1.0f);
        settle();
        CHECK (std::fabs (r.proc.getEngine().modDepth (0, pedwar::Engine::ModCoarse) - 3.0f) < 0.01f, "bus grid: beta's LFO on FM moves alpha +-3 octaves");
        CHECK (r.proc.getEngine().modDepth (3, pedwar::Engine::ModCoarse) < 1.0e-6f, "bus grid: delta not moved");
    }
    // the Morph readout
    {
        PedwarAudioProcessor p;
        auto* m = p.getValueTreeState().getParameter (id (0, Morph));
        const juce::String x = juce::String::fromUTF8 ("\xc3\x97");
        CHECK (m->getText (0.5f, 20) == x + "1" && m->getText (0.1f, 20) == x + "4" && m->getText (0.9f, 20) == x + juce::String::fromUTF8 ("\xc2\xbc"),
               "Morph readout x4 .. x1 .. x1/4 (%s)", m->getText (0.1f, 20).toRawUTF8());
        CHECK (m->getText (0.0f, 20) == "LP" && m->getText (1.0f, 20) == "HP / BP", "Morph readout at the ends");
        // readouts never print a long row of digits (milestone 3 fix)
        auto* q = p.getValueTreeState().getParameter (id (0, Q));
        auto* c = p.getValueTreeState().getParameter (id (0, Coarse));
        auto* o = p.getValueTreeState().getParameter (offset);
        CHECK (q->getText (q->convertTo0to1 (23.456f), 20) == "23" && q->getText (q->convertTo0to1 (2.345f), 20) == "2.3", "Q readout (%s)", q->getText (q->convertTo0to1 (23.456f), 20).toRawUTF8());
        CHECK (c->getText (c->convertTo0to1 (277.183f), 20) == "C#4 277 Hz" && c->getText (c->convertTo0to1 (1234.5f), 20) == "D#6 1.23 kHz", "Coarse readout (%s)", c->getText (c->convertTo0to1 (277.183f), 20).toRawUTF8());
        CHECK (o->getText (o->convertTo0to1 (0.7f), 20) == "+16 st", "Offset readout (%s)", o->getText (o->convertTo0to1 (0.7f), 20).toRawUTF8());
    }
}

// ------------------------------------------------------------------------------------------------ 4c. milestone 3

static void testMilestone3()
{
    using namespace PdParams;
    // Quality through the plugin: Logic's latency stays put, the sound carries on at the new Quality
    {
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) setReal (r.proc, id (k, Level), k == 0 ? 0.0f : -60.0f);
        setReal (r.proc, id (0, Mode), 2.0f);
        setReal (r.proc, id (0, Tap), 1.0f);
        setReal (r.proc, id (0, Coarse), 300.0f);
        setReal (r.proc, id (0, KeyTrack), 0.0f);
        const int lat = r.proc.getLatencySamples();
        long pos = 0;
        r.runFor (0.5, nullptr, nullptr, pos);
        for (float q : { 2.0f, 0.0f, 1.0f })
        {
            setReal (r.proc, quality, q);
            auto o = r.runFor (0.5, nullptr, nullptr, pos);
            double ms = 0.0;
            for (size_t i = o.size() / 2; i < o.size(); ++i) ms += (double) o[i] * o[i];
            const double amp = std::sqrt (2.0 * ms / (double) (o.size() / 2));
            CHECK (r.proc.getEngine().oversamplingFactor() == (2 << (int) q), "Quality %.0f reaches the engine", q);
            CHECK (std::fabs (amp - 0.5) < 0.06, "Quality %.0f: the Osc filter carries on (%.3f)", q, amp);
            CHECK (r.proc.getLatencySamples() == lat, "Quality %.0f: latency unchanged (%d)", q, r.proc.getLatencySamples());
        }
        // prepareToPlay at 8x reports the same latency as at 4x
        setReal (r.proc, quality, 2.0f);
        r.proc.prepareToPlay (48000.0, 512);
        CHECK (r.proc.getLatencySamples() == lat, "prepared at 8x: same latency");
    }
    // Stereo and Pitch Snap reach the engine through the controls
    {
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) { setReal (r.proc, id (k, KeyTrack), 0.0f); setReal (r.proc, id (k, Coarse), 270.0f); }
        long pos = 0;
        r.runFor (0.2, nullptr, nullptr, pos);
        CHECK (std::fabs (r.proc.getEngine().playedHz (0) - 270.0f) < 0.3f, "Snap Off: 270 Hz");
        setReal (r.proc, pitchSnap, 1.0f);
        r.runFor (0.2, nullptr, nullptr, pos);
        CHECK (std::fabs (r.proc.getEngine().playedHz (0) - 277.18f) < 0.3f, "Snap Semis: 277.18 Hz (%.2f)", r.proc.getEngine().playedHz (0));
        setReal (r.proc, pitchSnap, 2.0f);
        r.runFor (0.2, nullptr, nullptr, pos);
        CHECK (std::fabs (r.proc.getEngine().playedHz (0) - 261.63f) < 0.3f, "Snap Steps: C4 (%.2f)", r.proc.getEngine().playedHz (0));
        setReal (r.proc, pitchSnap, 1.0f);
        setReal (r.proc, pitchScale, 1.0f);   // Major
        setReal (r.proc, pitchKey, 1.0f);     // C#: C#4 is the key's note
        r.runFor (0.2, nullptr, nullptr, pos);
        CHECK (std::fabs (r.proc.getEngine().playedHz (0) - 277.18f) < 0.3f, "C# major: C#4 (%.2f)", r.proc.getEngine().playedHz (0));
    }
    {
        // uber: alpha alone, full width -> only the left side sounds
        Rig r (false);
        for (int k = 0; k < numFilters; ++k) setReal (r.proc, id (k, Level), k == 0 ? 0.0f : -60.0f);
        setReal (r.proc, id (0, Mode), 2.0f);
        setReal (r.proc, id (0, Width), 1.0f);
        setReal (r.proc, id (0, Morph), 0.0f);      // fully left: low-pass (at x1 the notch would take the sine out)
        setReal (r.proc, stereoMode, 1.0f);
        long pos = 0;
        r.runFor (0.5, nullptr, nullptr, pos);
        const int ch = juce::jmax (2, r.inCh);
        juce::AudioBuffer<float> buf (ch, 512);
        buf.clear();
        juce::MidiBuffer midi;
        r.proc.processBlock (buf, midi);
        CHECK (buf.getMagnitude (0, 0, 512) > 0.3f && buf.getMagnitude (1, 0, 512) < 1e-4f, "uber through the plugin: alpha on the left only (%.3f / %.5f)",
               buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
    }
}

// ------------------------------------------------------------------------------------------------ 5. panel

static void testPanel (const juce::File& snapDir)
{
    using namespace PdStyle;
    PedwarAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    CHECK (ed != nullptr, "panel opens");
    if (ed == nullptr) return;
    const double aspect = (double) ed->getWidth() / (double) ed->getHeight();
    CHECK (std::fabs (aspect - (double) panelW / (double) panelH) < 0.01, "panel proportions (%d x %d)", ed->getWidth(), ed->getHeight());
    CHECK (ed->getWidth() == panelW && ed->getHeight() == panelH, "opens at %d x %d", ed->getWidth(), ed->getHeight());

    auto& panelRef = dynamic_cast<PedwarAudioProcessorEditor&> (*ed).getPanel();
    auto* panel = &panelRef;
    int enabled = 0, dimmed = 0;
    for (auto* c : panel->getChildren())
    {
        if (c == &panelRef.getBackdrop()) continue;            // the fixed artwork, not a control
        if (c->isEnabled()) ++enabled;
        else
        {
            ++dimmed;
            CHECK (c->getAlpha() < 0.5f, "a disabled control is dimmed");
        }
    }
    // live: 4 x (mode, gain, in limit, coarse, fine, Q, FM, morph, width, morph ends, out limit, key trk, tap, strike)
    //       + 2 links + offset, split, rate, grid + 4 levels + master limit = 67
    //       + milestone 3: stereo, snap, scale, key, preset, quality = 73
    CHECK (enabled == 73, "%d live controls", enabled);
    CHECK (dimmed == 0, "%d dimmed controls", dimmed);

    // the Preset menu lists Init and every factory preset; the Quality menu marks 8x heavy
    {
        juce::ComboBox* preset = nullptr;
        juce::ComboBox* qual = nullptr;
        for (auto* c : panel->getChildren())
            if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
            {
                if (cb->getName() == "Preset") preset = cb;
                else if (cb->getNumItems() == 3 && cb->getItemText (0) == "2x") qual = cb;
            }
        CHECK (preset != nullptr && preset->getNumItems() == proc.getNumPrograms() && preset->getText() == "Init", "Preset menu: %d entries, shows Init",
               preset != nullptr ? preset->getNumItems() : 0);
        CHECK (qual != nullptr && qual->getItemText (2) == "8x heavy" && qual->getText() == "4x", "Quality menu: 2x / 4x / 8x heavy, on 4x");
        if (qual != nullptr)
        {
            setReal (proc, PdParams::quality, 2.0f);
            CHECK (qual->getText() == "8x heavy", "Quality on 8x: the menu shows \"%s\"", qual->getText().toRawUTF8());
            setReal (proc, PdParams::quality, 1.0f);
        }
        {
            // a project saved at 8x: the menu shows "8x heavy" when the window opens
            PedwarAudioProcessor p8;
            setReal (p8, PdParams::quality, 2.0f);
            std::unique_ptr<juce::AudioProcessorEditor> e8 (p8.createEditor());
            juce::ComboBox* q8 = nullptr;
            for (auto* c : dynamic_cast<PedwarAudioProcessorEditor&> (*e8).getPanel().getChildren())
                if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
                    if (cb->getNumItems() == 3 && cb->getItemText (0) == "2x") q8 = cb;
            CHECK (q8 != nullptr && q8->getText() == "8x heavy", "opened at 8x: the menu shows \"%s\"", q8 != nullptr ? q8->getText().toRawUTF8() : "");
        }
        if (preset != nullptr)
        {
            preset->setSelectedId (3, juce::sendNotificationSync);      // the second factory preset
            CHECK (proc.getCurrentProgram() == 2 && preset->getText() == proc.getProgramName (2), "choosing a preset in the menu sets it");
            proc.setCurrentProgram (0);
        }
    }
    // the Coarse readout shows the snapped note while Pitch Snap is on
    {
        setReal (proc, PdParams::id (0, PdParams::Coarse), 270.0f);
        PdKnob* alphaCoarse = nullptr;
        for (auto* c : panel->getChildren())
            if (auto* k = dynamic_cast<PdKnob*> (c))
                if (k->getBounds().getCentreX() < 120 && k->readoutText().endsWith ("Hz") && alphaCoarse == nullptr) alphaCoarse = k;
        CHECK (alphaCoarse != nullptr, "alpha's Coarse knob found");
        if (alphaCoarse != nullptr)
        {
            const auto plain = alphaCoarse->readoutText();
            setReal (proc, PdParams::pitchSnap, 1.0f);
            const auto snapped = alphaCoarse->readoutText();
            CHECK (plain == "C#4 270 Hz" && snapped == "C#4 277 Hz", "Coarse readout: %s, snapped %s", plain.toRawUTF8(), snapped.toRawUTF8());
            setReal (proc, PdParams::pitchSnap, 0.0f);
        }
        setReal (proc, PdParams::id (0, PdParams::Coarse), 110.0f);
    }

    // Modulation rings: alpha animated (Q and Morph) and moved by beta's LFO (FM): after some audio, the rings of
    // alpha's Coarse, Q and Morph knobs show a swing; beta's stay off.
    {
        setReal (proc, PdParams::busIds[0], 2.0f);       // alpha: anim
        setReal (proc, PdParams::id (0, PdParams::FmAmt), 0.5f);
        setReal (proc, PdParams::lfoRate, 5.0f);
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (juce::jmax (2, proc.getTotalNumInputChannels()), 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b) { buf.clear(); proc.processBlock (buf, midi); }
        int ringsOn = 0, ringsOff = 0;
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* k = dynamic_cast<PdKnob*> (&c))
            {
                k->refreshModulation();
                const auto m = k->shownModulation();
                if (m.depth > 0.01f) ++ringsOn; else ++ringsOff;
            }
            for (auto* ch : c.getChildren()) visit (*ch);
        };
        visit (*panel);
        CHECK (ringsOn == 3, "modulation rings showing: %d (alpha's coarse, Q, morph)", ringsOn);
        CHECK (proc.getEngine().modDepth (0, pedwar::Engine::ModCoarse) > 1.4f, "alpha's FM depth reaches the panel");
    }

    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    CHECK (img.isValid(), "panel draws");
    if (snapDir != juce::File())
    {
        snapDir.createDirectory();
        juce::PNGImageFormat png;
        auto f = snapDir.getChildFile (PedwarAudioProcessor::isSynth ? "panel-synth.png" : "panel.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
}

// ------------------------------------------------------------------------------------------------ 5b. the gold look

// The window sizes the panel is checked at (the corner can be dragged from 90% to 200%).
static const float kSizes[] = { 0.9f, 1.0f, 1.25f, 1.5f, 2.0f };

static std::unique_ptr<juce::AudioProcessorEditor> editorAt (PedwarAudioProcessor& proc, float s)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (ed != nullptr)
        ed->setSize (juce::roundToInt ((float) PdStyle::panelW * s), juce::roundToInt ((float) PdStyle::panelH * s));
    return ed;
}

// "PEDWAR" fits its title area: measured with the panel's own font, and in the drawn pixels at every window size.
static void testTitle()
{
    using namespace PdStyle;
    const float w = titleWidth();
    CHECK (w > 60.0f && w <= titleRight - titleLeft, "title width %.1f fits the title area (%.0f)", w, titleRight - titleLeft);
    std::printf ("title \"PEDWAR\": %.1f of %.0f panel units\n", w, titleRight - titleLeft);
    for (float s : kSizes)
    {
        PedwarAudioProcessor proc;
        auto ed = editorAt (proc, s);
        CHECK (ed != nullptr && std::abs (ed->getWidth() - juce::roundToInt (panelW * s)) <= 1, "size %.0f%%: window %d x %d",
               s * 100.0f, ed != nullptr ? ed->getWidth() : 0, ed != nullptr ? ed->getHeight() : 0);
        if (ed == nullptr) continue;
        const float k = (float) ed->getWidth() / (float) panelW;
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        CHECK (img.isValid() && img.getWidth() == ed->getWidth(), "size %.0f%%: the panel draws", s * 100.0f);
        // in the drawn pixels: the title's gold stays inside its area, and nothing bright sits in the gaps between the
        // title area and the two ornament lines
        int spill = 0, inside = 0;
        for (int y = (int) (8 * k); y < (int) (42 * k); ++y)
        {
            for (int x = (int) std::ceil (titleLeft * k); x < (int) (titleRight * k); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++inside;
            for (int x = (int) std::ceil ((ornamentLeftEnd + 1.5f) * k); x < (int) ((titleLeft - 0.5f) * k); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
            for (int x = (int) std::ceil ((titleRight + 0.5f) * k); x < (int) ((ornamentRightStart - 1.5f) * k); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
        }
        CHECK (inside > 60 && spill == 0, "size %.0f%%: title drawn inside its area (%d lit pixels inside, %d outside)", s * 100.0f, inside, spill);
    }
}

// Every text on the panel fits its place.
static void testTextsFit()
{
    using namespace PdStyle;
    PedwarAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
    auto& panel = dynamic_cast<PedwarAudioProcessorEditor&> (*edBase).getPanel();
    int tooWide = 0, texts = 0;
    float worst = 0.0f;                        // the tightest fit seen (width / room)
    auto fits = [&] (const juce::String& what, float width, float room)
    {
        ++texts;
        worst = std::max (worst, width / room);
        if (width > room)
        {
            if (++tooWide <= 30) std::printf ("  too wide: \"%s\" %.1f of %.1f\n", what.toRawUTF8(), width, room);
            return false;
        }
        return true;
    };
    for (const auto& t : panel.fixedTexts()) fits (t.text, t.width, t.room);
    for (auto* c : panel.getChildren())
    {
        if (auto* k = dynamic_cast<PdKnob*> (c))
        {
            // the value over the knob's whole range (with Pitch Snap off and on, Morph ends both ways)
            for (float snap : { 0.0f, 1.0f })
                for (float ends : { 0.0f, 1.0f })
                {
                    setReal (proc, PdParams::pitchSnap, snap);
                    for (int f = 0; f < PdParams::numFilters; ++f) setReal (proc, PdParams::id (f, PdParams::MorphEnds), ends);
                    for (int i = 0; i <= 400; ++i)
                    {
                        const auto t = k->readoutAt ((float) i / 400.0f);
                        fits (k->getComponentID() + ": " + t, widthOf (t, valueFont()), k->readoutRoom());
                    }
                }
            setReal (proc, PdParams::pitchSnap, 0.0f);
        }
        else if (auto* sw = dynamic_cast<PdSegments*> (c))
        {
            const int n = sw->getKind() == PdSegments::Kind::Option ? 1 : sw->getLabels().size();
            for (int i = 0; i < n; ++i)
                fits (sw->getComponentID() + ": " + sw->getLabels()[i], sw->neededWidth (i), sw->roomFor (i));
        }
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
        {
            const float room = (float) cb->getWidth() - 8.0f - 18.0f;
            for (int i = 0; i < cb->getNumItems(); ++i)
                fits (cb->getComponentID() + ": " + cb->getItemText (i), widthOf (cb->getItemText (i), menuFont()), room);
            if (cb->getComponentID() == "preset") fits ("Custom", widthOf ("Custom", menuFont()), room);
        }
        else if (auto* fd = dynamic_cast<PdFader*> (c))
        {
            for (const char* t : { "-inf", "+6", "-60", "-12" }) fits (t, widthOf (t, valueFont()), PdFader::kW);
            juce::ignoreUnused (fd);
        }
        else if (auto* l = dynamic_cast<PdStrikeLight*> (c))
            fits ("STRIKE", PdSegments::optionWidth ("STRIKE"), (float) l->getWidth());
        else if (auto* gr = dynamic_cast<PdBusGrid*> (c))
            for (const char* t : { "WIDTH", "FM", "ANIM" })
                fits (t, widthOf (t, labelFont()), PdBusGrid::rowLabelRoom ((float) gr->getWidth()));
    }
    for (const char* t : { "TRACK IN", "SIDE CHAIN" })
        fits (t, 16.0f + widthOf (t, labelFont()), 134.0f - 16.0f);
    CHECK (tooWide == 0 && texts > 2000, "every text on the panel fits its place (%d texts, %d too wide, tightest %.0f%% of its room)",
           texts, tooWide, worst * 100.0f);
    std::printf ("panel texts: %d checked, tightest fit %.0f%% of its room\n", texts, worst * 100.0f);
}

// The fixed artwork is drawn once into a stored picture, and 30 times a second only the live areas are redrawn.
// Measured here against drawing the whole panel every time (what 0.3.1 did), with the modulation rings showing.
static void testDrawingCost()
{
    PedwarAudioProcessor proc;
    setReal (proc, PdParams::busIds[0], 2.0f);
    setReal (proc, PdParams::busIds[2], 2.0f);
    setReal (proc, PdParams::id (0, PdParams::FmAmt), 0.5f);
    setReal (proc, PdParams::lfoRate, 5.0f);
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (juce::jmax (2, proc.getTotalNumInputChannels()), 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b) { buf.clear(); proc.processBlock (buf, midi); }
    for (float s : { 1.0f, 2.0f })
    {
        auto edBase = editorAt (proc, s);
        auto& panel = dynamic_cast<PedwarAudioProcessorEditor&> (*edBase).getPanel();
        const float k = (float) edBase->getWidth() / (float) PdStyle::panelW;
        std::function<void (juce::Component&)> refresh = [&] (juce::Component& c)
        {
            if (auto* kn = dynamic_cast<PdKnob*> (&c)) kn->refreshModulation();
            for (auto* ch : c.getChildren()) refresh (*ch);
        };
        refresh (panel);
        CHECK (panel.getBackdrop().getCachedComponentImage() != nullptr, "the panel's fixed artwork is stored as a picture");
        juce::Image img (juce::Image::ARGB, edBase->getWidth(), edBase->getHeight(), true);
        juce::RectangleList<int> live;
        for (const auto& a : panel.liveAreas())
            live.add (a.toFloat().transformedBy (juce::AffineTransform::scale (k)).getSmallestIntegerContainer().expanded (1));
        auto timeIt = [&] (bool liveOnly, int n)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
            {
                juce::Graphics g (img);
                if (liveOnly) g.reduceClipRegion (live);
                edBase->paintEntireComponent (g, true);
            }
            return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / n;
        };
        auto& backdrop = panel.getBackdrop();
        backdrop.setBufferedToImage (false);
        timeIt (false, 3);
        const double whole = timeIt (false, 20);
        backdrop.setBufferedToImage (true);
        timeIt (true, 3);
        const double tick = timeIt (true, 60);
        std::printf ("drawing at %.0f%%: live update %.2f ms, whole panel every time %.2f ms\n", s * 100.0f, tick, whole);
        CHECK (tick < 0.6 * whole, "size %.0f%%: the live update (%.2f ms) is much lighter than drawing the whole panel (%.2f ms)", s * 100.0f, tick, whole);
        CHECK (tick < 12.0, "size %.0f%%: the live update takes %.2f ms (limit 12 ms; one update every 33 ms)", s * 100.0f, tick);
    }
}

// Pictures of the panel (--snapshot <folder>): first load at 100%, the smallest and largest window sizes, and one
// with many switches changed (links on, über, Pitch Snap and a scale, 8x, other modes and taps) and rings showing.
static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    const juce::String tag = PedwarAudioProcessor::isSynth ? "synth-" : "";
    auto shoot = [&] (PedwarAudioProcessor& proc, float s, const juce::String& name)
    {
        auto ed = editorAt (proc, s);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile ("panel-" + tag + name + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    {
        PedwarAudioProcessor proc;
        shoot (proc, 1.0f, "first-load-100");
        shoot (proc, 0.9f, "size-90");
        shoot (proc, 2.0f, "size-200");
    }
    {
        PedwarAudioProcessor proc;
        using namespace PdParams;
        setReal (proc, linkAB, 1.0f);
        setReal (proc, linkGD, 1.0f);
        setReal (proc, stereoMode, 1.0f);
        setReal (proc, pitchSnap, 1.0f);
        setReal (proc, pitchScale, 3.0f);
        setReal (proc, pitchKey, 9.0f);
        setReal (proc, quality, 2.0f);
        setReal (proc, pitchSplit, 1.0f);
        setReal (proc, masterLimit, 1.0f);
        setReal (proc, id (1, Mode), 1.0f);
        setReal (proc, id (2, Mode), 2.0f);
        setReal (proc, id (0, InLimit), 1.0f);
        setReal (proc, id (3, Tap), 2.0f);
        setReal (proc, id (1, Strike), 0.0f);
        setReal (proc, id (2, KeyTrack), 0.0f);
        setReal (proc, id (3, OutLimit), 1.0f);
        setReal (proc, id (2, MorphEnds), 1.0f);
        setReal (proc, busIds[0], 2.0f);
        setReal (proc, busIds[1], 1.0f);
        setReal (proc, busIds[2], 2.0f);
        setReal (proc, id (0, FmAmt), 0.5f);
        setReal (proc, id (2, FmAmt), -0.4f);
        setReal (proc, lfoRate, 3.0f);
        setReal (proc, id (0, Coarse), 270.0f);
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (juce::jmax (2, proc.getTotalNumInputChannels()), 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b) { buf.clear(); proc.processBlock (buf, midi); }
        shoot (proc, 1.0f, "many-switches-100");
    }
}

// ------------------------------------------------------------------------------------------------ 6. automation

static void testAutomation()
{
    Rig r (true);
    auto params = allParams (r.proc);
    std::mt19937 g (99);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    bool finite = true;
    float pk = 0.0f;
    long pos = 0;
    for (int b = 0; b < 1500; ++b)
    {
        for (auto* p : params) if (g() % 3 == 0) p->setValueNotifyingHost (u (g));
        juce::MidiBuffer midi;
        if (g() % 4 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, (int) (g() % 128), (juce::uint8) (1 + g() % 127)), (int) (g() % 64));
        if (g() % 4 == 0) midi.addEvent (juce::MidiMessage::noteOff (1, (int) (g() % 128)), (int) (g() % 64));
        auto o = r.run (64, pos, [&] (long) { return u (g) * 2.0f - 1.0f; }, [&] (long n) { return (n / 4800) % 2 ? u (g) - 0.5f : 0.0f; }, midi);
        pos += 64;
        for (float x : o) { finite = finite && std::isfinite (x); pk = std::max (pk, std::fabs (x)); }
    }
    CHECK (finite, "random automation through the parameters: finite");
    CHECK (pk < 40.0f, "random automation: bounded (peak %.2f)", pk);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    bool writeGolden = false;
    juce::File snapDir;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--write-golden") writeGolden = true;
        if (juce::String (argv[i]) == "--snapshot" && i + 1 < argc) snapDir = juce::File (juce::String (argv[i + 1]));
    }
    const juce::File golden = juce::File (PEDWAR_GOLDEN_DIR).getChildFile ("layout-0.1.0.txt");
    {
        PedwarAudioProcessor proc;
        testControlList (proc, golden, writeGolden);
    }
    if (writeGolden) return 0;
    testState();
    testPresets();
    testAudio();
    testMilestone2();
    testMilestone3();
    testPanel (snapDir);
    testTitle();
    testTextsFit();
    testDrawingCost();
    if (snapDir != juce::File()) writeSnapshots (snapDir);
    testAutomation();
    std::printf ("plugin checks (%s): %d checks, %d failed\n", kWhich, checks, failures);
    return failures == 0 ? 0 : 1;
}
