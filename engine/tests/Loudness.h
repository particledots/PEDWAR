// Test helper: how loud a preset is, measured the same way for every preset (used by the engine self-test).
//
//   Input presets:  pink noise in (-18 dB RMS), no notes.
//   Voice presets:  no input, middle C held.
//   Struck presets: no input, middle C struck every quarter second (velocity 100).
// The loudness is the K-weighted mean square of both sides (ITU-R BS.1770, the measure behind LUFS), seconds 1 to 4,
// at 48 kHz, in dB.

#pragma once
#include "pedwar/Engine.h"
#include "pedwar/Presets.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace loudness
{
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

    // Pink noise (Paul Kellet's filter on white noise), the same sequence every time.
    struct Pink
    {
        uint32_t s = 22222u;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double white() { s = s * 1664525u + 1013904223u; return ((double) (s >> 8) / 8388608.0) - 1.0; }
        double next()
        {
            const double w = white();
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            return y;
        }
    };

    inline std::vector<float> pinkNoise (int n, double rmsDb = -18.0)
    {
        Pink p;
        std::vector<double> v ((size_t) n);
        double ms = 0.0;
        for (auto& x : v) { x = p.next(); ms += x * x; }
        const double g = std::pow (10.0, rmsDb / 20.0) / std::sqrt (ms / n);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i) out[(size_t) i] = (float) (v[(size_t) i] * g);
        return out;
    }

    // Loudness of a parameter set played as 'kind' (see the top of this file), in dB.
    inline double measure (const pedwar::Params& p, int kind, double* peakOut = nullptr)
    {
        using namespace pedwar;
        const double sr = 48000.0;
        const int n = (int) (4.0 * sr), from = (int) (1.0 * sr), block = 256;
        const auto noise = pinkNoise (n);
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
            e.process (kind == presets::Input ? noise.data() + pos : nullptr, nullptr, l.data(), r.data(), block);
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
