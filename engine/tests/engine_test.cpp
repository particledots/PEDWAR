// Self-test for the PEDWAR engine. Run automatically by the build; the build fails if any check fails.

#include "pedwar/Engine.h"
#include "pedwar/Nonlinear.h"
#include "pedwar/Resampling.h"
#include "pedwar/Scales.h"
#include "pedwar/Pitch.h"
#include "pedwar/Presets.h"
#include "Loudness.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <memory>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace pedwar;

static int failures = 0;
static int checks = 0;

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

static constexpr double kPi = 3.14159265358979323846;
static double gSr = 48000.0;
static const int kBlock = 256;

using Gen = std::function<float (long)>;      // input sample n -> value

struct Out { std::vector<float> l, r; };

// Only filter 'which' is heard (level 0 dB); the others are silent.
static Params solo (int which)
{
    Params p;
    for (int f = 0; f < kNumFilters; ++f) p.f[f].levelDb = (f == which) ? 0.0f : -60.0f;
    return p;
}

static Out render (Engine& e, const Params& p, double seconds, const Gen& track, const Gen& side = nullptr,
                   long startSample = 0)
{
    const long n = (long) (seconds * gSr);
    Out o;
    o.l.resize ((size_t) n);
    o.r.resize ((size_t) n);
    std::vector<float> t (kBlock), s (kBlock);
    for (long pos = 0; pos < n; pos += kBlock)
    {
        const int m = (int) std::min<long> (kBlock, n - pos);
        for (int i = 0; i < m; ++i)
        {
            t[(size_t) i] = track ? track (startSample + pos + i) : 0.0f;
            s[(size_t) i] = side ? side (startSample + pos + i) : 0.0f;
        }
        e.setParams (p);
        e.process (t.data(), side ? s.data() : nullptr, o.l.data() + pos, o.r.data() + pos, m);
    }
    return o;
}

static Gen sine (double hz, double amp) { return [=] (long n) { return (float) (amp * std::sin (2.0 * kPi * hz * (double) n / gSr)); }; }
static Gen silence() { return [] (long) { return 0.0f; }; }

static double rms (const std::vector<float>& v, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max<size_t> (1, v.size() - from));
}
static double peak (const std::vector<float>& v, size_t from = 0)
{
    double m = 0.0;
    for (size_t i = from; i < v.size(); ++i) m = std::max (m, (double) std::fabs (v[i]));
    return m;
}
static bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}
static double db (double g) { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

// Amplitude of the component at 'hz' (Goertzel over the given range).
static double toneAmp (const std::vector<float>& v, double hz, size_t from)
{
    const size_t n = v.size() - from;
    const double w = 2.0 * kPi * hz / gSr;
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));   // Hann
        re += v[from + i] * win * std::cos (w * (double) i);
        im += v[from + i] * win * std::sin (w * (double) i);
    }
    return 2.0 * std::sqrt (re * re + im * im) / ((double) n * 0.5);
}

static double risingCrossingsHz (const std::vector<float>& v, size_t from)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double pos = (double) (i - 1) + (double) (-v[i - 1]) / (double) (v[i] - v[i - 1]);
            if (first < 0.0) first = pos;
            last = pos;
            ++count;
        }
    if (count < 2) return 0.0;
    return (count - 1) * gSr / (last - first);
}

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

// ------------------------------------------------------------------------------------------------ tests

static void testMaths()
{
    nl::Knee k { 0.35, 0.35 };
    CHECK (std::fabs (k.f (0.2) - 0.2) < 1e-12, "knee is linear below the knee");
    CHECK (std::fabs (k.f (5.0) - 0.7) < 1e-12, "knee ceiling 0.7, got %g", k.f (5.0));
    // F' = f, checked numerically, across the whole curve
    int bad = 0;
    for (const auto& fn : { nl::Knee { 0.35, 0.35 }, nl::Knee { 0.6, 0.4 } })
        for (double x = -3.0; x <= 3.0; x += 0.01)
        {
            const double d = (fn.F (x + 1e-5) - fn.F (x - 1e-5)) / 2e-5;
            if (std::fabs (d - fn.f (x)) > 1e-4) ++bad;
        }
    nl::Tanh t;
    for (double x = -8.0; x <= 8.0; x += 0.01)
    {
        const double d = (t.F (x + 1e-5) - t.F (x - 1e-5)) / 2e-5;
        if (std::fabs (d - t.f (x)) > 1e-4) ++bad;
    }
    CHECK (bad == 0, "antiderivatives match their functions (%d bad points)", bad);
    CHECK (scales::kNumScales == 28, "28 scales");
    CHECK (std::fabs (dbToGain (0.0f) - 1.0f) < 1e-6f && dbToGain (-60.0f) == 0.0f, "dB mapping");
}

static void testResampling()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Upsampler u; Decimator d;
        u.prepare (rate, 4); d.prepare (rate, 4);
        const double lat = u.latency() + d.latency();
        for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
        {
            std::vector<float> out (8192);
            float os[4];
            for (size_t i = 0; i < out.size(); ++i)
            {
                u.process ((float) (0.5 * std::sin (2.0 * kPi * hz * (double) i / rate)), os);
                out[i] = d.process (os);
            }
            const double save = gSr; gSr = rate;
            const double a = toneAmp (out, hz, 2048);
            gSr = save;
            CHECK (std::fabs (db (a / 0.5)) < 0.3, "%.0f Hz at %.0f: passes at %.2f dB", hz, rate, db (a / 0.5));
        }
        CHECK (lat > 5.0 && lat < 80.0, "latency %.1f samples at %.0f", lat, rate);
    }
}

// Steady-state gain of one filter, tap and frequency, for a quiet sine.
static double gainAt (int tap, double fc, double q, double hz, int mode = ModeFilter)
{
    Params p = solo (Alpha);
    p.f[0].coarseHz = (float) fc; p.f[0].q = (float) q; p.f[0].tap = tap; p.f[0].mode = mode;
    Engine e; e.setParams (p); e.prepare (gSr, kBlock);
    const auto o = render (e, p, 1.0, sine (hz, 0.05));
    return toneAmp (o.l, hz, (size_t) (0.5 * gSr)) / 0.05;
}

static void testResponses()
{
    // Butterworth-like (Q 0.707): -3 dB at the cutoff, flat far below (LP) / far above (HP)
    CHECK (std::fabs (db (gainAt (TapLP, 1000, 0.7071, 1000)) + 3.01) < 0.3, "LP -3 dB at cutoff: %.2f", db (gainAt (TapLP, 1000, 0.7071, 1000)));
    CHECK (std::fabs (db (gainAt (TapHP, 1000, 0.7071, 1000)) + 3.01) < 0.3, "HP -3 dB at cutoff");
    CHECK (std::fabs (db (gainAt (TapLP, 1000, 0.7071, 100))) < 0.2, "LP flat below");
    CHECK (std::fabs (db (gainAt (TapHP, 1000, 0.7071, 10000))) < 0.3, "HP flat above");
    CHECK (db (gainAt (TapLP, 1000, 0.7071, 8000)) < -34.0, "LP 12 dB/oct above: %.1f", db (gainAt (TapLP, 1000, 0.7071, 8000)));
    CHECK (db (gainAt (TapHP, 1000, 0.7071, 125)) < -34.0, "HP 12 dB/oct below");
    // resonance: LP / BP peak = Q at the cutoff
    CHECK (std::fabs (db (gainAt (TapLP, 500, 8.0, 500)) - db (8.0)) < 0.4, "LP peak = Q (%.2f dB)", db (gainAt (TapLP, 500, 8.0, 500)));
    CHECK (std::fabs (db (gainAt (TapBP, 500, 8.0, 500)) - db (8.0)) < 0.4, "BP peak = Q");
    CHECK (db (gainAt (TapBP, 500, 8.0, 4000)) < -15.0, "BP falls off 6 dB/oct: %.1f", db (gainAt (TapBP, 500, 8.0, 4000)));
    // notch: deep at the centre, flat away from it; "stereo morph" at x1 (its default) is the same notch
    CHECK (db (gainAt (TapNotch, 2000, 1.0, 2000)) < -40.0, "notch deep: %.1f dB", db (gainAt (TapNotch, 2000, 1.0, 2000)));
    CHECK (db (gainAt (TapMorph, 2000, 1.0, 2000)) < -40.0, "stereo morph at x1 = notch");
    CHECK (std::fabs (db (gainAt (TapNotch, 2000, 1.0, 100))) < 0.3, "notch flat far away");
    // the cutoff is right at the top of the range too
    CHECK (std::fabs (db (gainAt (TapLP, 15000, 0.7071, 15000)) + 3.01) < 0.6, "LP -3 dB at 15 kHz: %.2f", db (gainAt (TapLP, 15000, 0.7071, 15000)));
    // Res mode below its oscillation point behaves like the filter
    CHECK (std::fabs (db (gainAt (TapLP, 1000, 0.7071, 300, ModeRes)) - db (gainAt (TapLP, 1000, 0.7071, 300))) < 0.5, "Res at low Q ~ filter");
}

static void testOscillation()
{
    // Osc mode: steady level and exact pitch across the range, clean sine on LP
    for (double hz : { 30.0, 110.0, 440.0, 2000.0, 8000.0, 16000.0 })
    {
        Params p = solo (Alpha);
        p.f[0].mode = ModeOsc; p.f[0].coarseHz = (float) hz; p.f[0].tap = TapLP; p.f[0].q = 2.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 4.0, silence());
        const size_t from = (size_t) (2.0 * gSr);
        const double level = rms (o.l, from) * std::sqrt (2.0);
        CHECK (std::fabs (db (level / Engine::kOscLevel)) < 1.0, "Osc %.0f Hz level %.3f", hz, level);
        if (hz < 10000.0)
        {
            const double f = risingCrossingsHz (o.l, from);
            CHECK (std::fabs (cents (f, hz)) < 2.0, "Osc %.0f Hz pitch %.3f Hz (%.2f ct)", hz, f, cents (f, hz));
            double h = 0.0;
            for (int k = 2; k <= 5; ++k) if (hz * k < 20000.0) h += std::pow (toneAmp (o.l, hz * k, from), 2.0);
            const double thd = db (std::sqrt (h) / toneAmp (o.l, hz, from));
            CHECK (thd < -50.0, "Osc %.0f Hz is a clean sine: harmonics %.1f dB", hz, thd);
        }
        CHECK (std::fabs (o.l[from] - o.r[from]) < 1e-6f, "LP tap: both sides the same");
    }
    // Res mode: oscillates at Q 50, not at Q 5 (no input)
    {
        Params p = solo (Alpha);
        p.f[0].mode = ModeRes; p.f[0].coarseHz = 440.0f; p.f[0].tap = TapLP; p.f[0].q = 50.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        auto o = render (e, p, 4.0, silence());
        const double lv = rms (o.l, (size_t) (3.0 * gSr)) * std::sqrt (2.0);
        CHECK (lv > 0.3 && lv < 0.7, "Res Q 50 self-oscillates: %.3f", lv);
        CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (3.0 * gSr)), 440.0)) < 3.0, "Res pitch");
        p.f[0].q = 5.0f;
        o = render (e, p, 3.0, silence());
        CHECK (rms (o.l, (size_t) (2.0 * gSr)) < 1e-4, "Res Q 5 dies away: %g", rms (o.l, (size_t) (2.0 * gSr)));
        p.f[0].q = 50.0f;
        o = render (e, p, 4.0, silence());
        CHECK (rms (o.l, (size_t) (3.0 * gSr)) > 0.2, "and starts again by itself at Q 50");
    }
    // Filter mode never self-oscillates, even at Q 50
    {
        Params p = solo (Alpha);
        p.f[0].coarseHz = 440.0f; p.f[0].tap = TapLP; p.f[0].q = 50.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 2.0, [] (long n) { return n == 100 ? 1.0f : 0.0f; });
        CHECK (rms (o.l, (size_t) (1.8 * gSr)) < 0.01, "Filter mode rings down");
    }
}

static void testKeyTrackAndStrike()
{
    Params p = solo (Alpha);
    p.f[0].mode = ModeOsc; p.f[0].coarseHz = 261.6256f; p.f[0].tap = TapLP;
    Engine e; e.setParams (p); e.prepare (gSr, kBlock);
    render (e, p, 1.0, silence());
    e.noteOn (72, 0.0f);
    auto o = render (e, p, 1.0, silence());
    CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 523.2511)) < 2.0, "key track: note 72 = C5");
    e.noteOn (48, 0.0f);
    o = render (e, p, 1.0, silence());
    CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 130.8128)) < 2.0, "last note wins: C3");
    e.noteOff (48);
    o = render (e, p, 1.0, silence());
    CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 523.2511)) < 2.0, "release goes back to the held C5");
    e.noteOff (72);
    o = render (e, p, 1.0, silence());
    CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 523.2511)) < 2.0, "nothing held: pitch stays");
    p.f[0].keyTrack = false;
    o = render (e, p, 1.0, silence());
    CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 261.6256)) < 2.0, "key track off: Coarse only");

    // Strike: rings at the filter's frequency, as loud as the velocity, only where switched on
    for (float vel : { 1.0f, 0.5f })
    {
        Params s = solo (Alpha);
        s.f[0].coarseHz = 1000.0f; s.f[0].q = 30.0f; s.f[0].tap = TapLP;
        Engine se; se.setParams (s); se.prepare (gSr, kBlock);
        render (se, s, 0.1, silence());
        se.noteOn (60, vel);
        const auto r = render (se, s, 0.5, silence());
        const double pk = peak (r.l);
        CHECK (std::fabs (db (pk / (Engine::kStrikeKick * vel))) < 3.0, "strike vel %.1f peak %.3f", vel, pk);
        CHECK (std::fabs (cents (risingCrossingsHz (r.l, 100), 1000.0)) < 10.0, "strike rings at the cutoff");
        CHECK (se.strikeCount (0) == 1 && se.strikeCount (1) == 1, "strike counted for the lights");
    }
    {
        Params s = solo (Alpha);
        s.f[0].coarseHz = 1000.0f; s.f[0].q = 30.0f; s.f[0].tap = TapLP; s.f[0].strike = false;
        Engine se; se.setParams (s); se.prepare (gSr, kBlock);
        render (se, s, 0.1, silence());
        se.noteOn (60, 1.0f);
        const auto r = render (se, s, 0.3, silence());
        CHECK (peak (r.l) < 1e-6, "strike off: no ping");
        CHECK (se.strikeCount (0) == 0, "strike off: light does not flash");
    }
}

static void testRouting()
{
    // gamma takes the side chain while it carries audio, the track otherwise
    Params p = solo (Gamma);
    p.f[2].coarseHz = 16.0f; p.f[2].q = 0.5f; p.f[2].tap = TapHP;      // nearly a straight wire above 100 Hz
    Engine e; e.setParams (p); e.prepare (gSr, kBlock);
    auto o = render (e, p, 0.5, sine (1000, 0.1));
    CHECK (std::fabs (db (toneAmp (o.l, 1000, (size_t) (0.25 * gSr)) / 0.1)) < 0.5, "gamma hears the track without side chain");
    o = render (e, p, 1.0, sine (1000, 0.1), sine (3000, 0.1), (long) (0.5 * gSr));
    CHECK (toneAmp (o.l, 1000, (size_t) (0.5 * gSr)) < 0.002, "gamma no longer hears the track with side chain");
    CHECK (std::fabs (db (toneAmp (o.l, 3000, (size_t) (0.5 * gSr)) / 0.1)) < 0.5, "gamma hears the side chain");
    CHECK (e.sidechainPresent(), "side chain shown as present");
    o = render (e, p, 1.5, sine (1000, 0.1), silence());
    CHECK (toneAmp (o.l, 1000, (size_t) (0.5 * gSr)) < 0.002, "silent side chain within 2 s: still the side chain");
    o = render (e, p, 1.5, sine (1000, 0.1), silence());
    CHECK (std::fabs (db (toneAmp (o.l, 1000, (size_t) (1.0 * gSr)) / 0.1)) < 0.5, "after 2 s of silence: back to the track");
    CHECK (! e.sidechainPresent(), "side chain shown as gone");

    // alpha always takes the track
    Params a = solo (Alpha);
    a.f[0].coarseHz = 16.0f; a.f[0].q = 0.5f; a.f[0].tap = TapHP;
    Engine ea; ea.setParams (a); ea.prepare (gSr, kBlock);
    o = render (ea, a, 0.6, sine (1000, 0.1), sine (3000, 0.1));
    CHECK (toneAmp (o.l, 3000, (size_t) (0.3 * gSr)) < 0.002 && toneAmp (o.l, 1000, (size_t) (0.3 * gSr)) > 0.09, "alpha: track only");

    // ... from the very first sample when both start together (no blip of the side chain)
    Engine eb; eb.setParams (a); eb.prepare (gSr, kBlock);
    o = render (eb, a, 0.1, sine (1000, 0.1), sine (3000, 0.1));
    CHECK (toneAmp (o.l, 3000, (size_t) (0.002 * gSr)) < 0.002, "alpha: no side chain when both start together");
    CHECK (! eb.filterUsesSidechain (0) && ! eb.filterUsesSidechain (1) && eb.filterUsesSidechain (2) && eb.filterUsesSidechain (3),
           "usage flags: alpha/beta track, gamma/delta side chain");

    // Instrument slot (MIDI-controlled effect): the track input is silent, so alpha and beta take the side chain too
    Engine ei; ei.setParams (a); ei.prepare (gSr, kBlock);
    o = render (ei, a, 0.6, silence(), sine (3000, 0.1));
    CHECK (std::fabs (db (toneAmp (o.l, 3000, (size_t) (0.1 * gSr)) / 0.1)) < 0.5, "instrument slot: alpha hears the side chain");
    for (int f = 0; f < kNumFilters; ++f)
        CHECK (ei.filterUsesSidechain (f), "instrument slot: filter %d shown on the side chain", f);
    // track audio arrives: alpha goes back to it at once (20 ms cross-fade)
    o = render (ei, a, 0.6, sine (1000, 0.1), sine (3000, 0.1));
    CHECK (toneAmp (o.l, 3000, (size_t) (0.1 * gSr)) < 0.002 && std::fabs (db (toneAmp (o.l, 1000, (size_t) (0.1 * gSr)) / 0.1)) < 0.5,
           "track returns: alpha back on the track");
    CHECK (! ei.filterUsesSidechain (0), "track returns: alpha shown on the track");
    // track silent for less than 2 s: alpha stays on the (silent) track
    o = render (ei, a, 1.5, silence(), sine (3000, 0.1));
    CHECK (toneAmp (o.l, 3000, (size_t) (0.1 * gSr)) < 0.002, "track gap under 2 s: alpha stays on the track");
    o = render (ei, a, 1.5, silence(), sine (3000, 0.1));
    CHECK (std::fabs (db (toneAmp (o.l, 3000, (size_t) (1.0 * gSr)) / 0.1)) < 0.5, "track silent 2 s: alpha takes the side chain");

    // PEDWAR Synth: the side chain feeds all four
    Engine es; es.setSidechainFeedsAll (true); es.setParams (a); es.prepare (gSr, kBlock);
    o = render (es, a, 0.6, nullptr, sine (3000, 0.1));
    CHECK (std::fabs (db (toneAmp (o.l, 3000, (size_t) (0.3 * gSr)) / 0.1)) < 0.5, "synth: side chain feeds alpha");

    // the mix: four filters at -12 dB each add up
    Params m;
    for (auto& f : m.f) { f.coarseHz = 16.0f; f.q = 0.5f; f.tap = TapHP; f.levelDb = -12.0f; }
    Engine em; em.setParams (m); em.prepare (gSr, kBlock);
    o = render (em, m, 0.6, sine (1000, 0.1));
    CHECK (std::fabs (db (toneAmp (o.l, 1000, (size_t) (0.3 * gSr)) / 0.1) - db (4.0 * dbToGain (-12.0f))) < 0.5, "four levels sum");
}

static void testLimiters()
{
    // In limit: unity for small signals, bounded and rounded for big ones
    auto wire = [] (Params& p) { p.f[0].coarseHz = 16.0f; p.f[0].q = 0.5f; p.f[0].tap = TapHP; };
    {
        Params p = solo (Alpha); wire (p); p.f[0].inLimit = true;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        auto o = render (e, p, 0.5, sine (500, 0.01));
        CHECK (std::fabs (db (toneAmp (o.l, 500, (size_t) (0.25 * gSr)) / 0.01)) < 0.1, "In limit: small signals at unity");
        p.f[0].gainDb = 12.0f;
        o = render (e, p, 0.5, sine (500, 1.0));
        CHECK (peak (o.l, (size_t) (0.25 * gSr)) < 1.0 / 1.6 + 0.05, "In limit at full gain: bounded %.3f", peak (o.l, (size_t) (0.25 * gSr)));
        const double h3 = toneAmp (o.l, 1500, (size_t) (0.25 * gSr)) / toneAmp (o.l, 500, (size_t) (0.25 * gSr));
        CHECK (db (h3) > -20.0, "In limit at full gain shapes hard (3rd harmonic %.1f dB)", db (h3));
        p.f[0].gainDb = -6.0f;
        o = render (e, p, 0.5, sine (500, 1.0));
        const double g3 = toneAmp (o.l, 1500, (size_t) (0.25 * gSr)) / toneAmp (o.l, 500, (size_t) (0.25 * gSr));
        CHECK (db (g3) < -25.0 && db (g3) > -60.0, "In limit at half gain: gentle (3rd harmonic %.1f dB)", db (g3));
    }
    // Out limit tames a screaming resonance
    {
        Params p = solo (Alpha);
        p.f[0].coarseHz = 500.0f; p.f[0].q = 50.0f; p.f[0].tap = TapBP; p.f[0].gainDb = 12.0f; p.f[0].outLimit = true;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 1.0, sine (500, 0.5));
        CHECK (peak (o.l, (size_t) (0.2 * gSr)) < 0.75, "Out limit: peak %.3f", peak (o.l, (size_t) (0.2 * gSr)));
        p.f[0].outLimit = false;
        const auto o2 = render (e, p, 1.0, sine (500, 0.5));
        CHECK (peak (o2.l, (size_t) (0.2 * gSr)) > 2.0 && peak (o2.l) < 6.0, "without it: loud but bounded by the rails %.2f", peak (o2.l));
    }
    // Master limit
    {
        Params p;
        for (auto& f : p.f) { f.coarseHz = 16.0f; f.q = 0.5f; f.tap = TapHP; f.levelDb = 6.0f; }
        p.masterLimit = true;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        auto o = render (e, p, 0.5, sine (300, 0.9));
        CHECK (peak (o.l, (size_t) (0.25 * gSr)) < 1.03, "Master limit: peak %.3f", peak (o.l, (size_t) (0.25 * gSr)));
        o = render (e, p, 0.5, sine (300, 0.02));
        CHECK (std::fabs (db (toneAmp (o.l, 300, (size_t) (0.25 * gSr)) / (0.02 * 4.0 * dbToGain (6.0f)))) < 0.1, "Master limit linear when quiet");
    }
}

// Largest sample-to-sample jump, relative to the largest a clean sine of this level and pitch could make.
static double jumpRatio (const std::vector<float>& v, size_t from, size_t to, double hz)
{
    double maxStep = 0.0, maxAbs = 0.0;
    for (size_t i = from + 1; i < to; ++i)
    {
        maxStep = std::max (maxStep, (double) std::fabs (v[i] - v[i - 1]));
        maxAbs = std::max (maxAbs, (double) std::fabs (v[i]));
    }
    return maxStep / std::max (1e-9, maxAbs * 2.0 * kPi * hz / gSr);
}

static void testNoClicks()
{
    // Every switch, flipped while a sine plays, must not make a jump bigger than the sine itself does.
    struct Flip { const char* name; std::function<void (Params&)> on; };
    const std::vector<Flip> flips = {
        { "In limit", [] (Params& p) { p.f[0].inLimit = true; } },
        { "Out limit", [] (Params& p) { p.f[0].outLimit = true; } },
        { "Master limit", [] (Params& p) { p.masterLimit = true; } },
        { "Tap LP", [] (Params& p) { p.f[0].tap = TapLP; } },
        { "Tap HP", [] (Params& p) { p.f[0].tap = TapHP; } },
        { "Tap BP", [] (Params& p) { p.f[0].tap = TapBP; } },
        { "Mode Res", [] (Params& p) { p.f[0].mode = ModeRes; } },
        { "Coarse jump", [] (Params& p) { p.f[0].coarseHz = 4000.0f; } },
        { "Q jump", [] (Params& p) { p.f[0].q = 40.0f; } },
        { "Gain jump", [] (Params& p) { p.f[0].gainDb = 12.0f; } },
        { "Level jump", [] (Params& p) { p.f[0].levelDb = 6.0f; } },
    };
    for (const auto& fl : flips)
    {
        Params p = solo (Alpha);
        p.f[0].coarseHz = 2000.0f; p.f[0].q = 0.7071f; p.f[0].tap = TapNotch;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        render (e, p, 0.3, sine (150, 0.3));
        Params q = p;
        fl.on (q);
        const auto o = render (e, q, 0.2, sine (150, 0.3), nullptr, (long) (0.3 * gSr));
        const double r = jumpRatio (o.l, 0, o.l.size(), 150.0);
        CHECK (r < 1.6, "%s: no click (jump ratio %.2f)", fl.name, r);
    }
    // Key-track jump of two octaves on a self-oscillating filter: smooth
    {
        Params p = solo (Alpha);
        p.f[0].mode = ModeOsc; p.f[0].coarseHz = 220.0f; p.f[0].tap = TapLP;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        render (e, p, 1.0, silence());
        e.noteOn (84, 0.0f);
        const auto o = render (e, p, 0.1, silence());
        CHECK (jumpRatio (o.l, 0, o.l.size(), 880.0) < 1.6, "key track jump: no click");
    }
}

static void testAutomationStress()
{
    // Heavy random automation of everything, every 32 samples, with random notes, at three sample rates.
    std::mt19937 rng (1234);
    auto u = [&] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (rng); };
    auto pick = [&] (int n) { return (int) (rng() % (unsigned) n); };
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        gSr = rate;
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool limited = pass == 1;
            Engine e; Params p; e.setParams (p); e.prepare (rate, 32);
            std::vector<float> t (32), s (32), l (32), r (32);
            double mx = 0.0;
            bool finite = true;
            const long total = (long) (6.0 * rate);
            long n = 0;
            for (long pos = 0; pos < total; pos += 32)
            {
                for (auto& f : p.f)
                {
                    f.mode = pick (3); f.gainDb = u (-60, 12); f.inLimit = pick (2); f.coarseHz = 16.0f * std::pow (1250.0f, u (0, 1));
                    f.fineCents = u (-100, 100); f.q = 0.5f * std::pow (100.0f, u (0, 1)); f.outLimit = limited || pick (2);
                    f.keyTrack = pick (2); f.tap = pick (5); f.strike = pick (2); f.levelDb = u (-60, 6);
                    f.fmAmt = u (-1, 1); f.morph = u (0, 1); f.morphEnds = pick (2); f.width = u (0, 1);
                }
                p.masterLimit = limited || pick (2);
                p.linkAB = pick (2); p.linkGD = pick (2); p.offset = u (-1, 1); p.pitchSplit = pick (2);
                p.lfoRate = 0.01f * std::pow (2000.0f, u (0, 1));
                for (auto& b : p.bus) b = pick (3);
                p.stereoMode = pick (2); p.snap = pick (3); p.scale = pick (29); p.key = pick (12);
                if (pick (64) == 0) p.quality = pick (3);
                if (pick (8) == 0) e.noteOn (pick (128), u (0, 1));
                if (pick (8) == 0) e.noteOff (pick (128));
                for (int i = 0; i < 32; ++i, ++n)
                {
                    t[(size_t) i] = u (-1, 1);
                    s[(size_t) i] = (n / 4800) % 2 ? u (-1, 1) : 0.0f;
                }
                e.setParams (p);
                e.process (t.data(), s.data(), l.data(), r.data(), 32);
                for (int i = 0; i < 32; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    mx = std::max (mx, (double) std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
                }
            }
            CHECK (finite, "random automation at %.0f Hz: finite", rate);
            // (the limiters hold the oversampled signal at 1.0; the steep down-sampling filter may ring a little above it
            // when everything jumps every 32 samples: 1.22 at most seen since milestone 3 added more random controls)
            if (limited) CHECK (mx < 1.3, "random automation, all limiters on, at %.0f Hz: peak %.3f", rate, mx);
            else         CHECK (mx < 40.0, "random automation at %.0f Hz: bounded, peak %.2f", rate, mx);
        }
    }
    gSr = 48000.0;
    // Fast sweeps of the cutoff over the whole range, every block, at Q 50 in all three modes: stable
    for (int mode = 0; mode < 3; ++mode)
    {
        Params p = solo (Alpha);
        p.f[0].mode = mode; p.f[0].q = 50.0f; p.f[0].tap = TapLP;
        Engine e; e.setParams (p); e.prepare (gSr, 64);
        std::vector<float> t (64), l (64), r (64);
        bool ok = true;
        double mx = 0.0;
        for (int b = 0; b < 3000; ++b)
        {
            p.f[0].coarseHz = (b % 2) ? 20000.0f : 16.0f;
            for (int i = 0; i < 64; ++i) t[(size_t) i] = 0.5f * (float) std::sin (0.1 * (b * 64 + i));
            e.setParams (p);
            e.process (t.data(), nullptr, l.data(), r.data(), 64);
            for (float x : l) { ok = ok && std::isfinite (x); mx = std::max (mx, (double) std::fabs (x)); }
        }
        CHECK (ok && mx < 40.0, "mode %d: sweeping 16 Hz <-> 20 kHz every block at Q 50 stays stable (peak %.2f)", mode, mx);
    }
}

// ------------------------------------------------------------------------------------------------ milestone 2

// The complex amplitude of the component at 'hz' (Hann window), for phase comparisons.
static std::complex<double> toneC (const std::vector<float>& v, double hz, size_t from, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    const size_t n = to - from;
    const double w = 2.0 * kPi * hz / gSr;
    std::complex<double> acc (0.0, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));
        acc += (double) v[from + i] * win * std::polar (1.0, -w * (double) (from + i));
    }
    return acc * (4.0 / (double) n);
}
static double phaseDeg (std::complex<double> a, std::complex<double> b)   // angle of a relative to b, -180..180
{
    return std::arg (a / b) * 180.0 / kPi;
}

// Frequency of a self-oscillating filter, measured over short windows.
static std::vector<double> pitchTrack (const std::vector<float>& v, size_t from, double windowSec)
{
    std::vector<double> r;
    const size_t w = (size_t) (windowSec * gSr);
    for (size_t a = from; a + w <= v.size(); a += w)
    {
        std::vector<float> seg (v.begin() + (long) a, v.begin() + (long) (a + w));
        const double f = risingCrossingsHz (seg, 0);
        if (f > 0.0) r.push_back (f);
    }
    return r;
}

static Params morphSolo (float morph, int ends, float width, double fc = 1000.0, double q = 0.7071)
{
    Params p = solo (Alpha);
    p.f[0].coarseHz = (float) fc; p.f[0].q = (float) q; p.f[0].tap = TapMorph;
    p.f[0].morph = morph; p.f[0].morphEnds = ends; p.f[0].width = width;
    return p;
}
static double morphGain (float morph, int ends, double hz, double fc = 1000.0)
{
    Params p = morphSolo (morph, ends, 0.0f, fc);
    Engine e; e.setParams (p); e.prepare (gSr, kBlock);
    const auto o = render (e, p, 1.0, sine (hz, 0.05));
    return toneAmp (o.l, hz, (size_t) (0.5 * gSr)) / 0.05;
}

static void testMorph()
{
    // the weights and the readout's multiples
    auto near = [] (MorphWeights w, float lp, float hp, float bp) { return std::fabs (w.lp - lp) < 1e-4f && std::fabs (w.hp - hp) < 1e-4f && std::fabs (w.bp - bp) < 1e-4f; };
    CHECK (near (morphWeights (0.5f, 0.0f), 1, 1, 0), "Morph x1 = notch (LP + HP)");
    CHECK (near (morphWeights (0.0f, 0.0f), 1, 0, 0) && near (morphWeights (0.0f, 1.0f), 1, 0, 0), "Morph fully left = LP");
    CHECK (near (morphWeights (1.0f, 0.0f), 0, 1, 0), "Morph fully right, ends LP-HP = HP");
    CHECK (near (morphWeights (1.0f, 1.0f), 0, 0, 1), "Morph fully right, ends BP = BP");
    CHECK (std::fabs (morphMultiple (0.1f) - 4.0f) < 1e-3f && std::fabs (morphMultiple (0.3f) - 2.0f) < 1e-3f
        && std::fabs (morphMultiple (0.5f) - 1.0f) < 1e-4f && std::fabs (morphMultiple (0.7f) - 0.5f) < 1e-4f
        && std::fabs (morphMultiple (0.9f) - 0.25f) < 1e-4f, "Morph marks x4 x2 x1 x1/2 x1/4");
    CHECK (morphMultiple (0.0f) == 0.0f && morphMultiple (1.0f) == -1.0f, "Morph ends marked");
    {
        int jumps = 0;
        MorphWeights prev = morphWeights (0.0f, 0.0f);
        for (int i = 1; i <= 1000; ++i)
        {
            const auto w = morphWeights ((float) i / 1000.0f, 0.0f);
            if (std::fabs (w.lp - prev.lp) > 0.02f || std::fabs (w.hp - prev.hp) > 0.02f) ++jumps;
            prev = w;
        }
        CHECK (jumps == 0, "Morph moves smoothly over the whole knob (%d jumps)", jumps);
    }
    // responses (width 0, so both sides carry the morph signal)
    CHECK (db (morphGain (0.1f, EndsLpHp, 4000)) < -30.0, "x4: notch at 4 kHz (%.1f dB)", db (morphGain (0.1f, EndsLpHp, 4000)));
    CHECK (std::fabs (db (morphGain (0.1f, EndsLpHp, 100))) < 0.5, "x4: lows pass at unity (%.2f dB)", db (morphGain (0.1f, EndsLpHp, 100)));
    CHECK (db (morphGain (0.1f, EndsLpHp, 15000)) < -18.0, "x4: leans low-pass (%.1f dB at 15 kHz)", db (morphGain (0.1f, EndsLpHp, 15000)));
    CHECK (db (morphGain (0.9f, EndsLpHp, 250)) < -30.0, "x1/4: notch at 250 Hz (%.1f dB)", db (morphGain (0.9f, EndsLpHp, 250)));
    CHECK (std::fabs (db (morphGain (0.9f, EndsLpHp, 12000))) < 0.5, "x1/4: highs pass at unity");
    CHECK (db (morphGain (0.9f, EndsLpHp, 40)) < -18.0, "x1/4: leans high-pass (%.1f dB at 40 Hz)", db (morphGain (0.9f, EndsLpHp, 40)));
    CHECK (std::fabs (db (morphGain (0.0f, EndsLpHp, 1000)) + 3.01) < 0.3 && std::fabs (db (morphGain (0.0f, EndsLpHp, 100))) < 0.2, "fully left: LP");
    CHECK (std::fabs (db (morphGain (1.0f, EndsLpHp, 1000)) + 3.01) < 0.3 && std::fabs (db (morphGain (1.0f, EndsLpHp, 10000))) < 0.3, "fully right (LP-HP): HP");
    CHECK (std::fabs (db (morphGain (1.0f, EndsBp, 1000)) + 3.01) < 0.3 && db (morphGain (1.0f, EndsBp, 100)) < -15.0, "fully right (BP): BP");

    // A self-oscillating filter shifts between sine and cosine (BP ends) or turns over (LP-HP ends) across the knob.
    auto oscL = [] (float morph, int ends)
    {
        Params p = morphSolo (morph, ends, 0.0f, 500.0, 2.0);
        p.f[0].mode = ModeOsc;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        return render (e, p, 2.0, silence()).l;
    };
    const auto left = oscL (0.0f, EndsBp), bp = oscL (1.0f, EndsBp), hp = oscL (1.0f, EndsLpHp);
    const size_t from = (size_t) (1.0 * gSr);
    const auto cL = toneC (left, 500.0, from), cB = toneC (bp, 500.0, from), cH = toneC (hp, 500.0, from);
    CHECK (std::fabs (std::fabs (phaseDeg (cB, cL)) - 90.0) < 5.0, "BP ends: 90 degrees between the two ends (%.1f)", phaseDeg (cB, cL));
    CHECK (std::fabs (db (std::abs (cB) / std::abs (cL))) < 1.0, "BP ends: same level at both ends (%.2f dB)", db (std::abs (cB) / std::abs (cL)));
    CHECK (std::fabs (std::fabs (phaseDeg (cH, cL)) - 180.0) < 5.0, "LP-HP ends: 180 degrees (%.1f)", phaseDeg (cH, cL));
}

static void testWidth()
{
    auto noise = [] { auto r = std::make_shared<std::mt19937> (5); return [r] (long) { return std::uniform_real_distribution<float> (-0.3f, 0.3f) (*r); }; };
    // width 0: both sides the same
    {
        Params p = morphSolo (0.3f, EndsLpHp, 0.0f);
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 0.5, noise());
        double d = 0.0;
        for (size_t i = 0; i < o.l.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - o.r[i]));
        CHECK (d < 1e-6, "width 0: left = right (%g)", d);
    }
    // width 100%, all-pass not swept (alpha LFO on FM): alpha's right side is the all-pass copy, same level, turned
    // 180 degrees at the filter's frequency; gamma's left side is the one
    for (int f : { (int) Alpha, (int) Gamma })
    {
        Params p = solo (f);
        p.f[f].coarseHz = 1000.0f; p.f[f].q = 0.7071f; p.f[f].tap = TapMorph; p.f[f].morph = 0.0f; p.f[f].width = 1.0f;
        p.bus[f] = BusFm;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 1.0, sine (300, 0.1));
        const size_t from = (size_t) (0.5 * gSr);
        const auto& dry = f == Alpha ? o.l : o.r;
        const auto& wet = f == Alpha ? o.r : o.l;
        CHECK (std::fabs (db (toneAmp (wet, 300, from) / toneAmp (dry, 300, from))) < 0.3, "width (filter %d): all-pass keeps the level", f);
        const double ph = phaseDeg (toneC (wet, 300, from), toneC (dry, 300, from));
        const double expect = -2.0 * 2.0 * std::atan (300.0 / 1000.0) * 180.0 / kPi;   // two first-order stages
        CHECK (std::fabs (ph - expect) < 3.0, "width (filter %d): all-pass phase %.1f, expected %.1f", f, ph, expect);
        auto o2 = render (e, p, 0.5, sine (1000, 0.1), nullptr, (long) gSr);
        CHECK (std::fabs (std::fabs (phaseDeg (toneC (f == Alpha ? o2.r : o2.l, 1000, 4800), toneC (f == Alpha ? o2.l : o2.r, 1000, 4800))) - 180.0) < 3.0,
               "width (filter %d): 180 degrees at the filter's frequency", f);
    }
    // equal power: the blended side keeps the level of the other for broadband sound
    {
        Params p = morphSolo (0.5f, EndsLpHp, 0.5f, 2000.0);
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 2.0, noise());
        // (the blend is equal-power by its law; how loud the sum is also depends on how alike the two copies are)
        const double r = db (rms (o.r, 4800) / rms (o.l, 4800));
        CHECK (std::fabs (r) < 3.0, "width 50%%: blended side within 3 dB of the other (%.2f dB)", r);
        Params f = p; f.f[0].width = 1.0f;
        Engine e1; e1.setParams (f); e1.prepare (gSr, kBlock);
        const auto o1 = render (e1, f, 2.0, noise());
        CHECK (std::fabs (db (rms (o1.r, 4800) / rms (o1.l, 4800))) < 0.3, "width 100%%: the all-pass copy keeps the level");
    }
    // the filter's own LFO sweeps the all-pass (bus "width"); on "FM" it stands still
    for (int bus : { (int) BusWidth, (int) BusFm })
    {
        Params p = morphSolo (0.0f, EndsLpHp, 1.0f);
        p.lfoRate = 2.0f;
        p.bus[0] = bus;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 2.0, sine (1000, 0.1));
        double lo = 1e9, hi = -1e9;
        for (size_t a = (size_t) (0.2 * gSr); a + 2400 <= o.l.size(); a += 2400)
        {
            double ph = phaseDeg (toneC (o.r, 1000, a, a + 2400), toneC (o.l, 1000, a, a + 2400));
            if (ph < 0.0) ph += 360.0;
            lo = std::min (lo, ph); hi = std::max (hi, ph);
        }
        if (bus == BusWidth) CHECK (hi - lo > 120.0, "width bus: the LFO sweeps the stereo phase (%.0f..%.0f)", lo, hi);
        else                 CHECK (hi - lo < 2.0, "FM bus: the all-pass stands still (%.1f..%.1f)", lo, hi);
    }
}

static Params oscSolo (int f, float hz)
{
    Params p = solo (f);
    for (auto& x : p.f) { x.mode = ModeOsc; x.tap = TapLP; x.coarseHz = hz; x.keyTrack = false; }
    return p;
}
static double pitchOf (Engine& e, const Params& p, int f, double seconds = 0.6)
{
    Params q = p;
    for (int k = 0; k < kNumFilters; ++k) q.f[k].levelDb = k == f ? 0.0f : -60.0f;
    const auto o = render (e, q, seconds, silence());
    return risingCrossingsHz (o.l, (size_t) (0.3 * gSr));
}

static void testLfosAndFm()
{
    // Rate: the four LFOs run at Rate x 1, 1.127, 0.871, 1.293
    {
        Params p; p.lfoRate = 2.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        int ups[kNumFilters] = {};
        float prev[kNumFilters];
        for (int j = 0; j < kNumFilters; ++j) prev[j] = e.lfoValue (j);
        std::vector<float> t (64, 0.0f), l (64), r (64);
        for (long n = 0; n < (long) (20.0 * gSr); n += 64)
        {
            e.setParams (p);
            e.process (t.data(), nullptr, l.data(), r.data(), 64);
            for (int j = 0; j < kNumFilters; ++j)
            {
                const float v = e.lfoValue (j);
                if (prev[j] < 0.0f && v >= 0.0f) ++ups[j];
                prev[j] = v;
            }
        }
        for (int j = 0; j < kNumFilters; ++j)
            CHECK (std::abs (ups[j] - 40.0 * Engine::kLfoRatio[j]) <= 1.0, "LFO %d: %d cycles in 20 s at 2 Hz (expected %.1f)", j, ups[j], 40.0 * Engine::kLfoRatio[j]);
    }
    // FM: beta's LFO on "FM" sweeps alpha's cutoff +-1 octave at FM amt 1/3
    {
        Params p = oscSolo (Alpha, 440.0f);
        p.lfoRate = 0.5f; p.bus[1] = BusFm; p.f[0].fmAmt = 1.0f / 3.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 5.0, silence());
        const auto tr = pitchTrack (o.l, (size_t) (0.2 * gSr), 0.02);
        const double mx = *std::max_element (tr.begin(), tr.end()), mn = *std::min_element (tr.begin(), tr.end());
        CHECK (cents (mx, 880.0) > -60.0 && cents (mx, 880.0) < 25.0, "FM: up to one octave above (%.1f Hz)", mx);
        CHECK (cents (mn, 220.0) < 60.0 && cents (mn, 220.0) > -25.0, "FM: down to one octave below (%.1f Hz)", mn);
        // polarity: beta's LFO starts at 0 heading down
        auto early = [&] (float amt)
        {
            Params q = p; q.f[0].fmAmt = amt;
            Engine x; x.setParams (q); x.prepare (gSr, kBlock);
            const auto y = render (x, q, 0.45, silence());
            return risingCrossingsHz (y.l, (size_t) (0.05 * gSr));
        };
        CHECK (early (0.3f) < 430.0 && early (-0.3f) > 450.0, "FM amt polarity (%.1f / %.1f Hz)", early (0.3f), early (-0.3f));
        p.f[0].fmAmt = 0.0f;
        Engine z; z.setParams (p); z.prepare (gSr, kBlock);
        const auto o0 = render (z, p, 2.0, silence());
        const auto t0 = pitchTrack (o0.l, (size_t) (0.2 * gSr), 0.05);
        CHECK (cents (*std::max_element (t0.begin(), t0.end()), *std::min_element (t0.begin(), t0.end())) < 3.0, "FM amt 0: steady");
    }
    // routing: alpha's LFO on "FM" moves delta, not alpha (each LFO moves the previous filter)
    {
        Params p = oscSolo (Alpha, 440.0f);
        p.lfoRate = 1.0f; p.bus[0] = BusFm;
        for (auto& f : p.f) f.fmAmt = 0.5f;
        auto spread = [&] (int f)
        {
            Params q = p;
            for (int k = 0; k < kNumFilters; ++k) q.f[k].levelDb = k == f ? 0.0f : -60.0f;
            Engine e; e.setParams (q); e.prepare (gSr, kBlock);
            const auto o = render (e, q, 2.5, silence());
            const auto t = pitchTrack (o.l, (size_t) (0.2 * gSr), 0.02);
            return cents (*std::max_element (t.begin(), t.end()), *std::min_element (t.begin(), t.end()));
        };
        CHECK (spread (Delta) > 1000.0 && spread (Alpha) < 3.0 && spread (Beta) < 3.0 && spread (Gamma) < 3.0,
               "FM bus: alpha's LFO moves delta only (%.0f ct; alpha %.1f)", spread (Delta), spread (Alpha));
    }
}

static void testAnim()
{
    Params p = solo (Alpha);
    p.f[0].coarseHz = 1000.0f; p.f[0].q = 8.0f; p.f[0].tap = TapBP; p.lfoRate = 1.0f; p.bus[0] = BusAnim;
    Engine e; e.setParams (p); e.prepare (gSr, kBlock);
    const auto o = render (e, p, 4.0, sine (1000, 0.02));
    CHECK (std::fabs (e.modDepth (0, Engine::ModQ) - Engine::kAnimQOctaves) < 1e-3 && std::fabs (e.modDepth (0, Engine::ModMorph) - Engine::kAnimMorph) < 1e-3,
           "anim: Q and Morph depths shown");
    CHECK (e.modDepth (0, Engine::ModCoarse) == 0.0f, "anim with FM amt 0: cutoff not moved");
    CHECK (e.modDepth (1, Engine::ModQ) == 0.0f && e.modDepth (2, Engine::ModMorph) == 0.0f, "anim on alpha only: the others still");
    double lo = 1e9, hi = 0.0;
    for (size_t a = (size_t) (0.3 * gSr); a + 2400 <= o.l.size(); a += 2400)
    {
        const double v = rms (std::vector<float> (o.l.begin() + (long) a, o.l.begin() + (long) (a + 2400)));
        lo = std::min (lo, v); hi = std::max (hi, v);
    }
    CHECK (db (hi / lo) > 12.0, "anim: Q is moved (band-pass level swings %.1f dB)", db (hi / lo));
    // anim nudges the LFO's speed
    Params w = p; w.bus[0] = BusWidth;
    Engine e2; e2.setParams (w); e2.prepare (gSr, kBlock);
    render (e2, w, 4.0, sine (1000, 0.02));
    CHECK (std::fabs (e.lfoValue (0) - e2.lfoValue (0)) > 0.01, "anim: alpha's LFO speed nudged by beta's");
    CHECK (std::fabs (e.lfoValue (1) - e2.lfoValue (1)) < 1e-6, "beta's LFO untouched");
}

static void testLinkOffsetSplit()
{
    // Link: alpha 1 octave above its default (110 Hz) takes beta 1 octave up too; delta leads gamma the same way
    {
        Params p = oscSolo (Beta, 220.0f);
        p.f[0].coarseHz = 220.0f; p.f[0].fineCents = 50.0f;
        p.f[3].coarseHz = 3520.0f; p.f[2].coarseHz = 440.0f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        CHECK (std::fabs (cents (pitchOf (e, p, Beta), 220.0)) < 2.0, "link off: beta at its own 220 Hz");
        p.linkAB = true; p.linkGD = true;
        const double b = pitchOf (e, p, Beta), g = pitchOf (e, p, Gamma), a = pitchOf (e, p, Alpha);
        CHECK (std::fabs (cents (b, 440.0 * std::exp2 (50.0 / 1200.0))) < 2.0, "link: beta follows alpha's Coarse and Fine (%.1f Hz)", b);
        CHECK (std::fabs (cents (g, 880.0)) < 2.0, "link: gamma follows delta (%.1f Hz)", g);
        CHECK (std::fabs (cents (a, 220.0 * std::exp2 (50.0 / 1200.0))) < 2.0, "link: alpha (the leader) unchanged");
        // the leader's LFO movement is passed on
        p.f[0].coarseHz = 110.0f; p.f[0].fineCents = 0.0f; p.f[0].fmAmt = 0.5f; p.bus[1] = BusFm; p.lfoRate = 1.0f;
        Params q = p;
        for (int k = 0; k < kNumFilters; ++k) q.f[k].levelDb = k == Beta ? 0.0f : -60.0f;
        Engine x; x.setParams (q); x.prepare (gSr, kBlock);
        const auto o = render (x, q, 2.5, silence());
        const auto t = pitchTrack (o.l, (size_t) (0.2 * gSr), 0.02);
        CHECK (cents (*std::max_element (t.begin(), t.end()), *std::min_element (t.begin(), t.end())) > 1000.0, "link: beta moves with alpha's FM");
        CHECK (x.modDepth (Beta, Engine::ModCoarse) > 1.4f, "link: beta's ring shows alpha's FM (%.2f oct)", x.modDepth (Beta, Engine::ModCoarse));
    }
    // Offset: S-curve, moves all four
    CHECK (offsetSemitones (1.0f) == 48.0f && offsetSemitones (0.5f) == 6.0f && std::fabs (offsetSemitones (0.1f)) < 0.05f, "Offset S-curve");
    {
        Params p = oscSolo (Alpha, 261.6256f);
        p.offset = 0.5f;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        for (int f = 0; f < kNumFilters; ++f)
            CHECK (std::fabs (cents (pitchOf (e, p, f), 369.9944)) < 2.0, "Offset +6 st moves filter %d", f);
        p.offset = -0.5f;
        CHECK (std::fabs (cents (pitchOf (e, p, Alpha), 184.9972)) < 2.0, "Offset -6 st");
    }
    // Pitch gamma-delta split: notes from middle C up move alpha and beta, notes below it gamma and delta
    {
        Params p = oscSolo (Alpha, 261.6256f);
        for (auto& f : p.f) f.keyTrack = true;
        p.pitchSplit = SplitSplit;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        e.noteOn (72, 0.0f);
        CHECK (std::fabs (cents (pitchOf (e, p, Alpha), 523.2511)) < 2.0 && std::fabs (cents (pitchOf (e, p, Beta), 523.2511)) < 2.0, "split: C5 moves alpha and beta");
        CHECK (std::fabs (cents (pitchOf (e, p, Gamma), 261.6256)) < 2.0 && std::fabs (cents (pitchOf (e, p, Delta), 261.6256)) < 2.0, "split: C5 leaves gamma and delta");
        e.noteOn (48, 0.0f);
        CHECK (std::fabs (cents (pitchOf (e, p, Gamma), 130.8128)) < 2.0 && std::fabs (cents (pitchOf (e, p, Delta), 130.8128)) < 2.0, "split: C3 moves gamma and delta");
        CHECK (std::fabs (cents (pitchOf (e, p, Alpha), 523.2511)) < 2.0, "split: alpha keeps C5");
        e.noteOff (48);
        CHECK (std::fabs (cents (pitchOf (e, p, Gamma), 130.8128)) < 2.0, "split: released low note, gamma stays");
        p.pitchSplit = SplitAll;
        CHECK (std::fabs (cents (pitchOf (e, p, Gamma), 523.2511)) < 2.0 && std::fabs (cents (pitchOf (e, p, Alpha), 523.2511)) < 2.0, "all: every filter follows the last note");
    }
}

static void testMilestone2NoClicks()
{
    // Each new control, changed while a sine plays through the stereo morph, must not make a jump bigger than the
    // sine itself does (on either side).
    struct Flip { const char* name; int solo; std::function<void (Params&)> setup, on; };
    auto none = [] (Params&) {};
    const std::vector<Flip> flips = {
        { "Morph jump", Alpha, none, [] (Params& p) { p.f[0].morph = 0.9f; } },
        { "Width jump", Alpha, none, [] (Params& p) { p.f[0].width = 1.0f; } },
        { "Morph ends (at the end)", Alpha, [] (Params& p) { p.f[0].morph = 1.0f; }, [] (Params& p) { p.f[0].morphEnds = EndsBp; } },
        { "Bus to FM", Alpha, none, [] (Params& p) { p.bus[1] = BusFm; } },
        { "Bus to anim", Alpha, none, [] (Params& p) { p.bus[0] = BusAnim; } },
        { "FM amt jump", Alpha, [] (Params& p) { p.bus[1] = BusFm; }, [] (Params& p) { p.f[0].fmAmt = -1.0f; } },
        { "Offset jump", Alpha, none, [] (Params& p) { p.offset = 0.6f; } },
        { "Link on", Beta, [] (Params& p) { p.f[0].coarseHz = 440.0f; }, [] (Params& p) { p.linkAB = true; } },
        { "Split on", Alpha, none, [] (Params& p) { p.pitchSplit = SplitSplit; } },
    };
    for (const auto& fl : flips)
    {
        Params p = solo (fl.solo);
        for (auto& f : p.f) { f.coarseHz = 2000.0f; f.q = 0.7071f; f.tap = TapMorph; f.fmAmt = 0.6f; }
        p.lfoRate = 3.0f;
        fl.setup (p);
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        e.noteOn (72, 0.0f);
        render (e, p, 0.3, sine (150, 0.3));
        Params q = p;
        fl.on (q);
        const auto o = render (e, q, 0.2, sine (150, 0.3), nullptr, (long) (0.3 * gSr));
        const double r = std::max (jumpRatio (o.l, 0, o.l.size(), 150.0), jumpRatio (o.r, 0, o.r.size(), 150.0));
        CHECK (r < 1.6, "%s: no click (jump ratio %.2f)", fl.name, r);
    }
}


// ------------------------------------------------------------------------------------------------ milestone 3

// Energy outside the harmonics of f0 (aliasing), relative to the fundamental, in dB. DFT on 10 Hz bins 100 Hz .. 17 kHz.
static double aliasDb (const std::vector<float>& v, size_t from, double f0)
{
    const size_t n = 4800;                      // 0.1 s: 10 Hz bins
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i) w[i] = (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1))) * v[from + i];
    double fund = 0.0, other = 0.0;
    for (double hz = 100.0; hz <= 17000.0; hz += 10.0)
    {
        double re = 0.0, im = 0.0;
        const double k = 2.0 * kPi * hz / gSr;
        for (size_t i = 0; i < n; ++i) { re += w[i] * std::cos (k * (double) i); im += w[i] * std::sin (k * (double) i); }
        const double pw = re * re + im * im;
        const double h = hz / f0;
        const double nearest = std::round (h);
        if (std::fabs (hz - f0) <= 30.0) fund += pw;
        else if (nearest >= 1.0 && std::fabs (hz - nearest * f0) <= 40.0) {}      // a real harmonic
        else other += pw;
    }
    return 10.0 * std::log10 (std::max (1e-30, other) / std::max (1e-30, fund));
}

static void testQuality()
{
    // Each Quality: the same latency (whole samples) at every sample rate, measured as well as reported
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const double save = gSr; gSr = rate;
        double lat[3] = {}, measured[3] = {};
        for (int q = 0; q < 3; ++q)
        {
            Params p = solo (Alpha);
            p.f[0].coarseHz = 16.0f; p.f[0].q = 0.5f; p.f[0].tap = TapHP;     // a near-straight wire
            p.quality = q;
            Engine e; e.setParams (p); e.prepare (rate, kBlock);
            CHECK (e.oversamplingFactor() == (2 << q), "Quality %d runs at %dx", q, e.oversamplingFactor());
            lat[q] = e.latencySamples();
            // the delay, from how the phase turns between 1000 and 1100 Hz (the wire itself adds almost nothing)
            auto phaseAt = [&] (double hz)
            {
                Engine x; x.setParams (p); x.prepare (rate, kBlock);
                const auto o = render (x, p, 0.5, sine (hz, 0.2));
                std::vector<float> ref ((size_t) (0.5 * rate));
                for (size_t i = 0; i < ref.size(); ++i) ref[i] = (float) (0.2 * std::sin (2.0 * kPi * hz * (double) i / rate));
                return phaseDeg (toneC (o.l, hz, (size_t) (0.2 * rate)), toneC (ref, hz, (size_t) (0.2 * rate)));
            };
            double d = phaseAt (1100.0) - phaseAt (1000.0);
            while (d > 0.0) d -= 360.0;
            measured[q] = -d / 360.0 * rate / 100.0;
        }
        CHECK (lat[0] == lat[1] && lat[1] == lat[2], "latency the same at every Quality at %.0f (%g %g %g)", rate, lat[0], lat[1], lat[2]);
        for (int q = 0; q < 3; ++q)
            CHECK (std::fabs (measured[q] - lat[q]) < 1.0, "Quality %d at %.0f: measured delay %.2f, reported %.0f", q, rate, measured[q], lat[q]);
        gSr = save;
    }

    // Each Quality: filter response, Osc mode level and pitch, Res self-oscillation
    for (int q : { 0, 2 })
    {
        auto lpAt = [q] (double fc, double hz)
        {
            Params p = solo (Alpha);
            p.f[0].coarseHz = (float) fc; p.f[0].q = 0.7071f; p.f[0].tap = TapLP; p.quality = q;
            Engine e; e.setParams (p); e.prepare (gSr, kBlock);
            const auto o = render (e, p, 1.0, sine (hz, 0.05));
            return db (toneAmp (o.l, hz, (size_t) (0.5 * gSr)) / 0.05);
        };
        CHECK (std::fabs (lpAt (1000, 1000) + 3.01) < 0.3, "Quality %d: LP -3 dB at 1 kHz (%.2f)", q, lpAt (1000, 1000));
        CHECK (std::fabs (lpAt (15000, 15000) + 3.01) < 0.8, "Quality %d: LP -3 dB at 15 kHz (%.2f)", q, lpAt (15000, 15000));
        for (double hz : { 110.0, 2000.0, 8000.0 })
        {
            Params p = oscSolo (Alpha, (float) hz);
            p.quality = q;
            Engine e; e.setParams (p); e.prepare (gSr, kBlock);
            const auto o = render (e, p, 3.0, silence());
            const size_t from = (size_t) (2.0 * gSr);
            const double level = rms (o.l, from) * std::sqrt (2.0);
            CHECK (std::fabs (db (level / Engine::kOscLevel)) < 1.0, "Quality %d: Osc %.0f Hz level %.3f", q, hz, level);
            CHECK (std::fabs (cents (risingCrossingsHz (o.l, from), hz)) < 2.0, "Quality %d: Osc %.0f Hz in tune", q, hz);
        }
        Params p = solo (Alpha);
        p.f[0].mode = ModeRes; p.f[0].coarseHz = 440.0f; p.f[0].tap = TapLP; p.f[0].q = 50.0f; p.quality = q;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto o = render (e, p, 4.0, silence());
        const double lv = rms (o.l, (size_t) (3.0 * gSr)) * std::sqrt (2.0);
        CHECK (lv > 0.3 && lv < 0.7, "Quality %d: Res Q 50 self-oscillates (%.3f)", q, lv);
    }

    // Aliasing of a hard-driven tanh: less at each step up
    {
        double al[3];
        for (int q = 0; q < 3; ++q)
        {
            Params p = solo (Alpha);
            p.f[0].coarseHz = 19000.0f; p.f[0].q = 0.5f; p.f[0].tap = TapLP;
            p.f[0].inLimit = true; p.f[0].gainDb = 12.0f; p.f[0].levelDb = -12.0f; p.quality = q;
            Engine e; e.setParams (p); e.prepare (gSr, kBlock);
            const auto o = render (e, p, 0.5, sine (7310.0, 0.9));
            al[q] = aliasDb (o.l, (size_t) (0.3 * gSr), 7310.0);
        }
        std::printf ("aliasing (driven tanh, 7.31 kHz): 2x %.1f dB, 4x %.1f dB, 8x %.1f dB\n", al[0], al[1], al[2]);
        CHECK (al[1] < al[0] - 6.0 && al[2] < al[1] + 1.0 && al[1] < -60.0, "aliasing falls with Quality (%.1f, %.1f, %.1f dB)", al[0], al[1], al[2]);
    }

    // Switching Quality while a filter self-oscillates: a short dip, no click, same pitch and level after
    for (int from : { 0, 1, 2 })
        for (int to : { 0, 1, 2 })
        {
            if (from == to) continue;
            Params p = oscSolo (Alpha, 300.0f);
            p.quality = from;
            Engine e; e.setParams (p); e.prepare (gSr, kBlock);
            render (e, p, 1.0, silence());
            Params q = p; q.quality = to;
            const auto o = render (e, q, 0.6, silence(), nullptr, (long) gSr);
            CHECK (e.oversamplingFactor() == (2 << to) && ! e.qualitySwitching(), "Quality %d -> %d switched", from, to);
            CHECK (jumpRatio (o.l, 0, o.l.size(), 300.0) < 1.6, "Quality %d -> %d: no click (jump ratio %.2f)", from, to, jumpRatio (o.l, 0, o.l.size(), 300.0));
            double dipMin = 1e9;
            for (size_t a = 0; a + 96 <= (size_t) (0.1 * gSr); a += 48) dipMin = std::min (dipMin, rms (std::vector<float> (o.l.begin() + (long) a, o.l.begin() + (long) (a + 96))));
            const double after = rms (o.l, (size_t) (0.3 * gSr)) * std::sqrt (2.0);
            CHECK (dipMin < 0.2, "Quality %d -> %d: fades through silence (%.3f)", from, to, dipMin);
            CHECK (std::fabs (db (after / Engine::kOscLevel)) < 0.5, "Quality %d -> %d: level after %.3f", from, to, after);
            CHECK (std::fabs (cents (risingCrossingsHz (o.l, (size_t) (0.3 * gSr)), 300.0)) < 2.0, "Quality %d -> %d: pitch after", from, to);
        }
    // A Quality change back before the first one has finished still ends on the last choice
    {
        Params p = oscSolo (Alpha, 300.0f);
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        render (e, p, 0.5, silence());
        Params q = p; q.quality = 2;
        render (e, q, 0.002, silence());
        q.quality = 0;
        render (e, q, 0.005, silence());
        q.quality = 1;
        const auto o = render (e, q, 0.5, silence());
        CHECK (e.oversamplingFactor() == 4 && ! e.qualitySwitching(), "quick back-and-forth ends on 4x");
        CHECK (rms (o.l, (size_t) (0.3 * gSr)) > 0.3, "and sounds");
    }
}

static void testUber()
{
    // alpha alone, full width: uber sends its right side (the all-pass blend) to the left, +3 dB; the right is silent
    for (int f : { Alpha, Gamma })
    {
        Params p = oscSolo (f, 500.0f);
        for (auto& x : p.f) { x.tap = TapMorph; x.morph = 0.0f; x.width = 1.0f; x.fmAmt = 0.0f; }
        for (auto& b : p.bus) b = BusFm;     // the all-pass stands still (FM amt 0: nothing moves)
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto sum = render (e, p, 1.0, silence());
        Params u = p; u.stereoMode = StereoUber;
        Engine e2; e2.setParams (u); e2.prepare (gSr, kBlock);
        const auto ub = render (e2, u, 1.0, silence());
        const size_t from = (size_t) (0.5 * gSr);
        const auto& blended = f < Gamma ? sum.r : sum.l;       // the all-pass side in sum mode
        const auto& lit = f < Gamma ? ub.l : ub.r;
        const auto& dark = f < Gamma ? ub.r : ub.l;
        CHECK (rms (dark, from) < 1e-4, "uber, %s alone: the %s side is silent (%g)", f == Alpha ? "alpha" : "gamma", f == Alpha ? "right" : "left", rms (dark, from));
        CHECK (std::fabs (db (rms (lit, from) / (rms (blended, from) * Engine::kUberGain))) < 0.1, "uber: the all-pass side, +4 dB");
        // and it is the all-pass side, not the plain one: the phase differs from the plain side by 180 degrees at full width
        const double ph = phaseDeg (toneC (lit, 500.0, from), toneC (f < Gamma ? sum.l : sum.r, 500.0, from));
        CHECK (std::fabs (std::fabs (ph) - 180.0) < 20.0, "uber takes the all-pass side (%.0f degrees)", ph);
    }
    // all four on noise: about as loud as sum, whether the four are much alike (Init: four notches) or not (band-passes)
    for (int bp = 0; bp < 2; ++bp)
    {
        Params p = presets::init();
        if (bp) for (auto& x : p.f) { x.tap = TapBP; x.q = 3.0f; }
        auto in = loudness::pinkNoise ((int) (2.0 * gSr));
        auto gen = [&in] (long n) { return in[(size_t) n % in.size()]; };
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        const auto s = render (e, p, 2.0, gen);
        Params u = p; u.stereoMode = StereoUber;
        Engine e2; e2.setParams (u); e2.prepare (gSr, kBlock);
        const auto o = render (e2, u, 2.0, gen);
        const size_t from = (size_t) (0.5 * gSr);
        const double ds = db (std::hypot (rms (o.l, from), rms (o.r, from)) / std::hypot (rms (s.l, from), rms (s.r, from)));
        CHECK (std::fabs (ds) < 1.5, "uber about as loud as sum (%s: %.2f dB)", bp ? "band-passes" : "Init", ds);
    }
    // switching: no click
    {
        Params p = solo (Alpha);
        for (auto& x : p.f) { x.coarseHz = 2000.0f; x.q = 0.7071f; x.width = 0.7f; x.levelDb = -6.0f; }
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        render (e, p, 0.3, sine (150, 0.3));
        Params u = p; u.stereoMode = StereoUber;
        const auto o = render (e, u, 0.2, sine (150, 0.3), nullptr, (long) (0.3 * gSr));
        CHECK (std::max (jumpRatio (o.l, 0, o.l.size(), 150.0), jumpRatio (o.r, 0, o.r.size(), 150.0)) < 1.6, "sum -> uber: no click");
    }
}

static void testPitchSnap()
{
    // Off: exactly the old tuning
    {
        std::mt19937 rng (5);
        int same = 0;
        for (int i = 0; i < 1000; ++i)
        {
            FilterParams fp;
            fp.coarseHz = 16.0f * std::pow (1250.0f, std::uniform_real_distribution<float> (0, 1) (rng));
            fp.fineCents = std::uniform_real_distribution<float> (-100, 100) (rng);
            fp.keyTrack = i % 2 == 0;
            const int note = (int) (rng() % 128);
            const float old = fp.coarseHz * std::exp2 (fp.fineCents / 1200.0f + (fp.keyTrack ? (float) (note - 60) : 0.0f) / 12.0f);
            same += Engine::targetHz (fp, note, pitch::SnapOff, 0, (int) (rng() % 12)) == old ? 1 : 0;
        }
        CHECK (same == 1000, "Snap Off, Scale Off: tuning unchanged (%d of 1000)", same);
    }
    auto hz = [] (float coarse, float fine, int snap, int scale, int key, bool kt = false, int note = 60)
    {
        FilterParams fp; fp.coarseHz = coarse; fp.fineCents = fine; fp.keyTrack = kt;
        return Engine::targetHz (fp, note, snap, scale, key);
    };
    auto near = [] (float a, float b) { return std::fabs (cents (a, b)) < 0.05; };
    CHECK (near (hz (270.0f, 0, pitch::SnapSemitones, 0, 0), 277.1826f), "Semitones: 270 Hz -> C#4");
    CHECK (near (hz (261.6256f, 60.0f, pitch::SnapSemitones, 0, 0), 277.1826f), "Semitones: C4 + 60 cents (Fine counts) -> C#4");
    CHECK (near (hz (261.6256f, 40.0f, pitch::SnapSemitones, 0, 0), 261.6256f), "Semitones: C4 + 40 cents -> C4");
    CHECK (near (hz (370.0f, 0, pitch::SnapSteps, 0, 0), 391.9954f), "Steps: F#4 -> G4 (a fifth)");
    CHECK (near (hz (200.0f, 0, pitch::SnapSteps, 0, 0), 174.6141f), "Steps: 200 Hz -> F3 (a fifth below C4)");
    CHECK (near (hz (16.0f, 0, pitch::SnapSteps, 0, 0), 16.35160f), "Steps: the bottom of the range -> C0");
    CHECK (near (hz (20000.0f, 0, pitch::SnapSteps, 0, 0), 16744.04f), "Steps: the top of the range -> C10");
    // Scale and Key
    const int major = 1, minorPent = 18;
    CHECK (near (hz (300.0f, 0, pitch::SnapSemitones, major, 0), 293.6648f), "C major: 300 Hz -> D4");
    CHECK (near (hz (277.1826f, 0, pitch::SnapSemitones, major, 0), 261.6256f), "C major: C#4 -> C4 (tie: nearer C4)");
    CHECK (near (hz (277.1826f, 0, pitch::SnapSemitones, major, 2), 277.1826f), "D major: C#4 stays");
    CHECK (near (hz (275.0f, 0, pitch::SnapOff, major, 0), 261.6256f), "Scale on, Snap Off: whole notes of the scale (275 Hz -> C4)");
    CHECK (near (hz (340.0f, 0, pitch::SnapSemitones, minorPent, 9), 329.6276f), "A minor pentatonic: 340 Hz -> E4");
    // with key tracking, the scale is around the note played; the note itself is always allowed
    CHECK (near (hz (261.6256f, 0, pitch::SnapSemitones, major, 0, true, 61), 277.1826f), "C major, key track, C#4 played, Coarse C4: plays C#4");
    CHECK (near (hz (290.0f, 0, pitch::SnapSemitones, major, 0, true, 64), 349.2282f), "C major, key track, E4 played, Coarse 290 Hz: F4 (F#4 is not in the scale)");
    CHECK (near (hz (290.0f, 0, pitch::SnapSemitones, 0, 0, true, 64), 369.9944f), "no scale, key track, E4 played, Coarse 290 Hz: F#4");
    // Steps with a scale that has no fifth (Locrian): only octaves
    CHECK (near (hz (360.0f, 0, pitch::SnapSteps, 7, 0), 261.6256f), "Locrian Steps: 360 Hz -> C4 (Locrian has no fifth)");
    CHECK (near (hz (360.0f, 0, pitch::SnapSteps, 1, 0), 391.9954f), "Major Steps: 360 Hz -> G4");

    // In the engine: an Osc filter plays the snapped pitch; Link adds the leader's snapped tuning; Offset is not snapped
    {
        Params p = oscSolo (Alpha, 270.0f);
        p.snap = pitch::SnapSemitones;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        CHECK (std::fabs (cents (pitchOf (e, p, Alpha), 277.1826)) < 2.0, "Osc plays the snapped pitch");
        p.offset = 0.2f;     // 48 * 0.008 = 0.384 semitones, not snapped
        CHECK (std::fabs (cents (pitchOf (e, p, Alpha), 277.1826) - 38.4) < 2.0, "Offset is added after the snap");
    }
    {
        Params p = oscSolo (Beta, 220.0f);
        p.f[0].coarseHz = 230.0f;           // alpha: snaps to A#3 = 233.08 Hz, 13 semitones above its default 110 Hz
        p.linkAB = true;
        p.snap = pitch::SnapSemitones;
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        CHECK (std::fabs (cents (pitchOf (e, p, Beta), 466.1638)) < 2.0, "Link adds the leader's snapped tuning (beta at A#4)");
    }
    // Changing Snap while a filter sings: the pitch glides (no click)
    {
        Params p = oscSolo (Alpha, 270.0f);
        Engine e; e.setParams (p); e.prepare (gSr, kBlock);
        render (e, p, 1.0, silence());
        Params q = p; q.snap = pitch::SnapSemitones;
        const auto o = render (e, q, 0.1, silence(), nullptr, (long) gSr);
        CHECK (jumpRatio (o.l, 0, o.l.size(), 277.0) < 1.6, "Snap on while singing: no click");
    }
}

static void testPresets()
{
    const double target = loudness::measure (presets::init(), presets::Input);
    std::printf ("preset loudness (Init with pink noise: %.1f dB, K-weighted):\n", target);
    for (int i = 0; i < presets::kNumFactory; ++i)
    {
        const auto& in = presets::info (i);
        const Params p = presets::factory (i);
        // never the player's own set-up
        const Params d = presets::init();
        CHECK (p.snap == d.snap && p.scale == d.scale && p.key == d.key && p.quality == d.quality, "preset %d leaves Snap, Scale, Key, Quality alone", i);
        CHECK (std::string (in.name).size() > 0 && std::string (in.name).size() < 24, "preset %d has a short name", i);
        double pk = 0.0;
        const double l = loudness::measure (p, in.kind, &pk);
        std::printf ("  %-20s %-6s %6.1f dB (%+.1f), peak %.2f\n", in.name, in.kind == presets::Input ? "input" : (in.kind == presets::Voice ? "voice" : "struck"), l, l - target, pk);
        CHECK (std::fabs (l - target) < 1.0, "preset %s level matched (%+.2f dB)", in.name, l - target);
        CHECK (pk < 1.0, "preset %s peak %.2f", in.name, pk);
        for (const auto& f : p.f)
            CHECK (f.levelDb <= 6.0f && f.levelDb >= -60.0f && f.q >= 0.5f && f.q <= 50.0f && f.coarseHz >= 16.0f && f.coarseHz <= 20000.0f
                   && f.gainDb <= 12.0f && f.fmAmt >= -1.0f && f.fmAmt <= 1.0f && f.morph >= 0.0f && f.morph <= 1.0f, "preset %d: settings in range", i);
        CHECK (p.lfoRate >= 0.01f && p.lfoRate <= 20.0f, "preset %d: rate in range", i);
        // every preset also plays cleanly at 2x and 8x, at 44.1 and 96 kHz
        for (int q : { 0, 2 })
        {
            Params r = p; r.quality = q;
            Engine e; e.setParams (r); e.prepare (q == 0 ? 44100.0 : 96000.0, kBlock);
            const double save = gSr; gSr = q == 0 ? 44100.0 : 96000.0;
            if (in.kind != presets::Input) e.noteOn (60, 0.8f);
            const auto o = render (e, r, 1.0, in.kind == presets::Input ? sine (220.0, 0.3) : silence());
            gSr = save;
            CHECK (allFinite (o.l) && allFinite (o.r) && peak (o.l) < 2.0 && rms (o.l) > 1e-3, "preset %s at %dx: plays, finite", in.name, 2 << q);
        }
    }
}

static void testCpu()
{
    Params p;
    for (int f = 0; f < kNumFilters; ++f)
    {
        p.f[f].mode = f % 3; p.f[f].inLimit = true; p.f[f].outLimit = true; p.f[f].q = 10.0f;
        p.f[f].coarseHz = 200.0f * (float) (f + 1); p.f[f].tap = f == 0 ? TapLP : TapMorph;
        p.f[f].fmAmt = 0.5f; p.f[f].width = 0.6f; p.f[f].morph = 0.3f;
    }
    p.masterLimit = true;
    p.linkAB = p.linkGD = true;
    p.lfoRate = 3.0f;
    p.bus[0] = BusWidth; p.bus[1] = BusFm; p.bus[2] = BusAnim; p.bus[3] = BusAnim;   // every connection busy
    p.stereoMode = StereoUber;
    p.snap = pitch::SnapSemitones; p.scale = 1;
    double pcts[3] = {};
    for (int q = 0; q < 3; ++q)
    {
        p.quality = q;
        Engine e; e.setParams (p); e.prepare (48000.0, 512);
        std::vector<float> t (512), s (512), l (512), r (512);
        for (int i = 0; i < 512; ++i) { t[(size_t) i] = 0.3f * (float) std::sin (i * 0.05); s[(size_t) i] = 0.3f * (float) std::sin (i * 0.031); }
        const int blocks = (int) (10.0 * 48000.0 / 512.0);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            e.setParams (p);
            e.process (t.data(), s.data(), l.data(), r.data(), 512);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        pcts[q] = 100.0 * secs / 10.0;
        if (q == 1) std::printf ("latency: %.1f samples at 48 kHz (every Quality)\n", e.latencySamples());
    }
    std::printf ("CPU: everything on (limiters, LFOs on every bus, Link, uber, snap), side chain playing, %% of one core at 48 kHz: 2x %.2f, 4x %.2f, 8x %.2f\n", pcts[0], pcts[1], pcts[2]);
    CHECK (pcts[1] < 25.0 && pcts[2] < 50.0, "CPU 4x %.2f%%, 8x %.2f%%", pcts[1], pcts[2]);
}

int main()
{
    testMaths();
    testResampling();
    testResponses();
    testOscillation();
    testKeyTrackAndStrike();
    testRouting();
    testLimiters();
    testNoClicks();
    testMorph();
    testWidth();
    testLfosAndFm();
    testAnim();
    testLinkOffsetSplit();
    testMilestone2NoClicks();
    testQuality();
    testUber();
    testPitchSnap();
    testPresets();
    testAutomationStress();
    testCpu();
    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
