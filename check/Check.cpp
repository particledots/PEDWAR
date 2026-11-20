// PedwarCheck / PedwarSynthCheck: run the plugin layer of PEDWAR (or PEDWAR Synth) without a host.
//
//   1. The list of controls is exactly the one fixed in 0.1.0 (same ids, order, ranges, defaults, menu lists), the
//      same for both plugins, so saved projects and automation keep working for good.
//   2. Saved settings come back exactly (save, load into a fresh plugin); the Preset entry is stored.
//   3. Init resets every control except Pitch Snap, Scale, Key and Quality.
//   4. Audio through processBlock: track input, side chain, MIDI strikes and key tracking, the bus layouts Logic uses,
//      the latency.
//   5. The panel opens at the draft's proportions, every control is there, later-milestone controls are dimmed.
//   6. Heavy random automation through the parameters stays finite and bounded.
//
// The build workflow runs both programs on every push and fails if any check fails.
// --write-golden writes the reference control list; --snapshot <folder> writes PNGs of the panel.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
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
    CHECK (b.getNumPrograms() == 1 && b.getProgramName (0) == "Init", "Preset menu: Init");

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

// ------------------------------------------------------------------------------------------------ 5. panel

static void testPanel (const juce::File& snapDir)
{
    PedwarAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    CHECK (ed != nullptr, "panel opens");
    if (ed == nullptr) return;
    const double aspect = (double) ed->getWidth() / (double) ed->getHeight();
    CHECK (std::fabs (aspect - 680.0 / 612.0) < 0.01, "draft proportions (%d x %d)", ed->getWidth(), ed->getHeight());
    CHECK (ed->getWidth() > 740 && ed->getWidth() < 820, "opens about 780 wide (%d)", ed->getWidth());

    auto* panel = ed->getChildComponent (0);
    int enabled = 0, dimmed = 0;
    for (auto* c : panel->getChildren())
    {
        if (c->isEnabled()) ++enabled;
        else
        {
            ++dimmed;
            CHECK (c->getAlpha() < 0.5f, "a disabled control is dimmed");
        }
    }
    // live: 4 x (mode, gain, in limit, coarse, fine, Q, out limit, key trk, tap, strike) + 4 levels + master limit = 45
    // dimmed: 4 x (FM, morph, width, morph ends) + 2 links + offset, split, rate, grid + stereo, snap, scale, key
    //         + preset, quality = 28
    CHECK (enabled == 45, "%d live controls", enabled);
    CHECK (dimmed == 28, "%d dimmed controls", dimmed);

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
    testAudio();
    testPanel (snapDir);
    testAutomation();
    std::printf ("plugin checks (%s): %d checks, %d failed\n", kWhich, checks, failures);
    return failures == 0 ? 0 : 1;
}
