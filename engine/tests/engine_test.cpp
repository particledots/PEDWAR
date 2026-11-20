// Self-test for the PEDWAR engine. Run automatically by the build; the build fails if any check fails.

#include "pedwar/Engine.h"
#include "pedwar/Nonlinear.h"
#include "pedwar/Resampling.h"
#include "pedwar/Scales.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    // notch: deep at the centre, flat away from it; "stereo morph" is the same notch until milestone 2
    CHECK (db (gainAt (TapNotch, 2000, 1.0, 2000)) < -40.0, "notch deep: %.1f dB", db (gainAt (TapNotch, 2000, 1.0, 2000)));
    CHECK (db (gainAt (TapMorph, 2000, 1.0, 2000)) < -40.0, "stereo morph = notch in milestone 1");
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
        CHECK (std::fabs (o.l[from] - o.r[from]) < 1e-6f, "both sides the same in milestone 1");
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
                }
                p.masterLimit = limited || pick (2);
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
            if (limited) CHECK (mx < 1.1, "random automation, all limiters on, at %.0f Hz: peak %.3f", rate, mx);
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

static void testCpu()
{
    Params p;
    for (int f = 0; f < kNumFilters; ++f)
    {
        p.f[f].mode = f % 3; p.f[f].inLimit = true; p.f[f].outLimit = true; p.f[f].q = 10.0f;
        p.f[f].coarseHz = 200.0f * (float) (f + 1); p.f[f].tap = TapLP;
    }
    p.masterLimit = true;
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
    const double pct = 100.0 * secs / 10.0;
    std::printf ("CPU: everything on, side chain playing: %.2f%% of one core at 48 kHz (4x oversampling)\n", pct);
    std::printf ("latency: %.1f samples at 48 kHz\n", e.latencySamples());
    CHECK (pct < 25.0, "CPU %.2f%%", pct);
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
    testAutomationStress();
    testCpu();
    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
