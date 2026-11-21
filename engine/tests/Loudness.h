// Test helper: how loud a preset is, measured the same way for every preset (used by the engine self-test).
//
//   Input presets:  audio in, no notes. Three test signals, each 4 seconds, the same every time:
//                     Pink:  pink noise, -18 dB RMS.
//                     Drums: a drum loop at 120 BPM (kick, snare, closed and open hats), peaks at -3 dB.
//                     Bass:  a bass line at 120 BPM (a filtered saw, eighth notes around C1 to C3), peaks at -3 dB.
//   Voice presets:  no input, middle C held.
//   Struck presets: no input, middle C struck every quarter second (velocity 100).
// The loudness is the K-weighted mean square of both sides (ITU-R BS.1770, the measure behind LUFS), seconds 1 to 4,
// at 48 kHz, in dB. The peak is the largest sample of either side over the same seconds (what Logic's meter shows).

#pragma once
#include "pedwar/Engine.h"
#include "pedwar/Presets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace loudness
{
    enum Signal { Pink = 0, Drums = 1, Bass = 2, kNumSignals = 3 };
    inline const char* signalName (int s) { return s == Pink ? "pink noise" : (s == Drums ? "drum loop" : "bass line"); }

    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
        double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    // K-weighting at 48 kHz (BS.1770): a high shelf and a high-pass.
    struct KWeight
    {
        Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        Biquad hp { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
        double run (double x) { return hp.run (shelf.run (x)); }
    };

    // White noise (a fixed sequence) and pink noise (Paul Kellet's filter on it).
    struct White
    {
        uint32_t s;
        explicit White (uint32_t seed = 22222u) : s (seed) {}
        double next() { s = s * 1664525u + 1013904223u; return ((double) (s >> 8) / 8388608.0) - 1.0; }
    };
    struct PinkGen
    {
        White w;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double next()
        {
            const double x = w.next();
            b0 = 0.99886 * b0 + x * 0.0555179; b1 = 0.99332 * b1 + x * 0.0750759; b2 = 0.96900 * b2 + x * 0.1538520;
            b3 = 0.86650 * b3 + x * 0.3104856; b4 = 0.55000 * b4 + x * 0.5329522; b5 = -0.7616 * b5 - x * 0.0168980;
            const double y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + x * 0.5362;
            b6 = x * 0.115926;
            return y;
        }
    };

    inline std::vector<float> toFloat (const std::vector<double>& v, double gain)
    {
        std::vector<float> out (v.size());
        for (size_t i = 0; i < v.size(); ++i) out[i] = (float) (v[i] * gain);
        return out;
    }
    inline double peakOf (const std::vector<double>& v) { double p = 0.0; for (double x : v) p = std::max (p, std::fabs (x)); return p; }

    inline std::vector<float> pinkNoise (int n, double rmsDb = -18.0)
    {
        PinkGen p;
        std::vector<double> v ((size_t) n);
        double ms = 0.0;
        for (auto& x : v) { x = p.next(); ms += x * x; }
        return toFloat (v, std::pow (10.0, rmsDb / 20.0) / std::sqrt (ms / n));
    }

    // One-pole filters for shaping the drum and bass sounds.
    struct OnePole
    {
        double a, z = 0.0;
        OnePole (double hz, double sr) : a (std::exp (-2.0 * M_PI * hz / sr)) {}
        double lp (double x) { z = x + a * (z - x); return z; }
        double hp (double x) { return x - lp (x); }
    };

    // A drum loop at 120 BPM, 16 sixteenths per bar: kick on 1, the "and" of 2 and 3; snare on 2 and 4; closed hats on
    // every eighth, an open hat on the last eighth of each bar. Its spectrum is close to a typical mixed drum loop's
    // (most energy from 50 to 300 Hz, falling slowly above). Peaks at peakDb.
    inline std::vector<float> drumLoop (int n, double sr, double peakDb = -3.0)
    {
        const double pi = M_PI, sixteenth = 60.0 / 120.0 / 4.0;
        const int step = (int) std::lround (sixteenth * sr);
        std::vector<double> v ((size_t) n, 0.0);
        White noise (777u);
        PinkGen pink; pink.w = White (999u);
        auto add = [&] (int at, double seconds, auto voice)
        {
            const int len = std::min (n - at, (int) (seconds * sr));
            for (int i = 0; i < len; ++i) v[(size_t) (at + i)] += voice ((double) i / sr);
        };
        for (int s = 0; s * step < n; ++s)
        {
            const int at = s * step, pos = s % 16;
            if (pos == 0 || pos == 6 || pos == 10)        // kick: a falling sine, slightly driven, with a beater click
            {
                OnePole hp (1500.0, sr);
                add (at, 0.45, [&] (double t)
                {
                    const double f0 = 50.0, f1 = 160.0, k = 30.0;
                    const double ph = 2.0 * pi * (f0 * t + (f1 - f0) * (1.0 - std::exp (-k * t)) / k);
                    return 0.8 * std::tanh (1.5 * std::sin (ph) * std::exp (-t * 7.0)) / 1.5
                         + 0.25 * hp.hp (noise.next()) * std::exp (-t * 250.0);
                });
            }
            if (pos == 4 || pos == 12)                    // snare: two tones and a band of noise
            {
                OnePole hp (150.0, sr), lp (5000.0, sr);
                add (at, 0.35, [&] (double t)
                {
                    const double body = 0.7 * (std::sin (2.0 * pi * 185.0 * t) + 0.5 * std::sin (2.0 * pi * 330.0 * t)) * std::exp (-t * 22.0);
                    return body + 0.7 * lp.lp (hp.hp (pink.next())) * std::exp (-t * 13.0);
                });
            }
            if (pos % 2 == 0)                             // hats: high-passed noise, short (open: longer)
            {
                OnePole hp (6000.0, sr);
                const bool open = pos == 14;
                const double decay = open ? 9.0 : 50.0, amp = (pos % 4 == 0 ? 0.03 : 0.7 * 0.03) * (open ? 0.8 : 1.0);
                add (at, open ? 0.4 : 0.1, [&] (double t) { return amp * hp.hp (noise.next()) * std::exp (-t * decay); });
            }
        }
        // a mix-bus style soft limit, so the peaks sit about as far above the loudness as in a typical mixed loop
        const double pk = peakOf (v);
        for (auto& x : v) x = std::tanh (2.5 * x / pk);
        return toFloat (v, std::pow (10.0, peakDb / 20.0) / peakOf (v));
    }

    // A bass line at 120 BPM: eighth notes (C1 to C3) of a saw through a two-pole low-pass at about 3 kHz, each with a
    // short decay. Peaks at peakDb.
    inline std::vector<float> bassLine (int n, double sr, double peakDb = -3.0)
    {
        const int notes[16] = { 36, 36, 48, 36, 39, 39, 43, 36, 34, 34, 46, 34, 41, 43, 39, 31 };
        const int eighth = (int) std::lround (60.0 / 120.0 / 2.0 * sr);
        std::vector<double> v ((size_t) n, 0.0);
        double ph = 0.0;
        OnePole lp1 (3000.0, sr), lp2 (3000.0, sr);
        for (int i = 0; i < n; ++i)
        {
            const int k = i / eighth;
            const double t = (double) (i - k * eighth) / sr, len = 0.23;
            const double hz = 440.0 * std::pow (2.0, (notes[k % 16] - 69) / 12.0);
            ph += hz / sr; ph -= std::floor (ph);
            const double env = t < 0.003 ? t / 0.003 : (t < len ? std::exp (-(t - 0.003) * 3.0) : std::exp (-(len - 0.003) * 3.0) * std::exp (-(t - len) * 120.0));
            v[(size_t) i] = lp2.lp (lp1.lp ((2.0 * ph - 1.0) * env));
        }
        return toFloat (v, std::pow (10.0, peakDb / 20.0) / peakOf (v));
    }

    inline std::vector<float> signal (int which, int n, double sr)
    {
        return which == Drums ? drumLoop (n, sr) : (which == Bass ? bassLine (n, sr) : pinkNoise (n));
    }

    // Loudness of a parameter set played as 'kind' (see the top of this file), in dB. Input presets hear 'which'.
    inline double measure (const pedwar::Params& p, int kind, double* peakOut = nullptr, int which = Pink)
    {
        using namespace pedwar;
        const double sr = 48000.0;
        const int n = (int) (4.0 * sr), from = (int) (1.0 * sr), block = 256;
        const auto in = signal (which, n, sr);
        Engine e;
        e.setParams (p);
        e.prepare (sr, block);
        KWeight kl, kr;
        std::vector<float> l ((size_t) block), r ((size_t) block);
        double ms = 0.0, pk = 0.0;
        const int strikeEvery = (int) (0.25 * sr);
        if (kind == presets::Voice) e.noteOn (60, 100.0f / 127.0f);
        for (int pos = 0; pos < n; pos += block)
        {
            if (kind == presets::Struck && pos % strikeEvery < block) { e.noteOff (60); e.noteOn (60, 100.0f / 127.0f); }
            e.setParams (p);
            e.process (kind == presets::Input ? in.data() + pos : nullptr, nullptr, l.data(), r.data(), block);
            for (int i = 0; i < block; ++i)
            {
                const double a = kl.run (l[(size_t) i]), b = kr.run (r[(size_t) i]);
                if (pos + i >= from)
                {
                    ms += 0.5 * (a * a + b * b);
                    pk = std::max (pk, (double) std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
                }
            }
        }
        if (peakOut != nullptr) *peakOut = pk;
        return 10.0 * std::log10 (std::max (1.0e-20, ms / (double) (n - from)));
    }
} // namespace loudness
