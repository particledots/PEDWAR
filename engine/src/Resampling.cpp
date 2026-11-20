#include "pedwar/Resampling.h"

#include <algorithm>
#include <cmath>

namespace pedwar
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-12) break;
        }
        return sum;
    }

    // Dot product with eight running sums, so the compiler can use the vector units.
    inline float dot (const float* a, const float* b, int n)
    {
        float s[8] = {};
        int k = 0;
        for (; k + 8 <= n; k += 8)
            for (int j = 0; j < 8; ++j)
                s[j] += a[k + j] * b[k + j];
        float acc = ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        for (; k < n; ++k)
            acc += a[k] * b[k];
        return acc;
    }
}

std::vector<float> designLowpass (double baseRate, int factor)
{
    const int fac = std::max (1, factor);
    const double osRate = baseRate * fac;

    const double passEdge = std::min (20000.0, 0.45 * baseRate);
    const double stopEdge = baseRate - passEdge;
    const double cutoff = 0.5 * (passEdge + stopEdge) / osRate;
    const double width = std::max (1.0e-3, (stopEdge - passEdge) / osRate);
    const double atten = 100.0;
    const double beta = 0.1102 * (atten - 8.7);

    int n = (int) std::ceil ((atten - 7.95) / (2.285 * 2.0 * kPi * width)) + 1;
    n = std::clamp (n, 15, 1023);
    if ((n & 1) == 0) ++n;

    std::vector<double> t ((size_t) n);
    const double mid = 0.5 * (n - 1);
    const double i0b = besselI0 (beta);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = i - mid;
        const double sinc = (x == 0.0) ? 2.0 * cutoff : std::sin (2.0 * kPi * cutoff * x) / (kPi * x);
        const double r = x / mid;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        t[(size_t) i] = sinc * w;
        sum += t[(size_t) i];
    }
    std::vector<float> taps ((size_t) n);
    for (int i = 0; i < n; ++i)
        taps[(size_t) i] = (float) (t[(size_t) i] / sum);
    return taps;
}

// ------------------------------------------------------------------------------------------------ Upsampler

void Upsampler::prepare (double baseRate, int factor)
{
    fac = std::max (1, factor);
    auto taps = designLowpass (baseRate, fac);
    numTaps = (int) taps.size();
    perPhase = (numTaps + fac - 1) / fac;
    phases.assign ((size_t) (fac * perPhase), 0.0f);
    // Output phase p uses taps p, p + fac, p + 2 fac ... ; the gain of 'fac' makes up for the zeros put in between.
    for (int p = 0; p < fac; ++p)
        for (int k = 0; k < perPhase; ++k)
        {
            const int i = p + k * fac;
            phases[(size_t) (p * perPhase + k)] = i < numTaps ? taps[(size_t) i] * (float) fac : 0.0f;
        }
    hist.assign ((size_t) perPhase * 2, 0.0f);
    pos = 0;
    zeroRun = perPhase;
}

void Upsampler::reset()
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    pos = 0;
    zeroRun = perPhase;
}

void Upsampler::process (float x, float* out)
{
    // Silence in, and nothing left in the history: silence out, without the arithmetic (an unused side chain).
    if (x == 0.0f)
    {
        if (zeroRun >= perPhase)
        {
            for (int p = 0; p < fac; ++p) out[p] = 0.0f;
            return;
        }
        ++zeroRun;
    }
    else
        zeroRun = 0;

    pos = (pos == 0) ? perPhase - 1 : pos - 1;
    hist[(size_t) pos] = hist[(size_t) (pos + perPhase)] = x;
    const float* h = hist.data() + pos;           // h[0] = newest
    for (int p = 0; p < fac; ++p)
    {
        out[p] = dot (phases.data() + p * perPhase, h, perPhase);
    }
}

void Upsampler::push (float x)
{
    if (x == 0.0f)
    {
        if (zeroRun >= perPhase) return;          // already all silence
        ++zeroRun;
    }
    else
        zeroRun = 0;
    pos = (pos == 0) ? perPhase - 1 : pos - 1;
    hist[(size_t) pos] = hist[(size_t) (pos + perPhase)] = x;
}

// ------------------------------------------------------------------------------------------------ Decimator

void Decimator::prepare (double baseRate, int factor)
{
    fac = std::max (1, factor);
    taps = designLowpass (baseRate, fac);
    buf.assign (taps.size() * 2, 0.0f);
    pos = 0;
}

void Decimator::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    pos = 0;
}

float Decimator::process (const float* in)
{
    const int n = (int) taps.size();
    for (int i = 0; i < fac; ++i)
    {
        pos = (pos == 0) ? n - 1 : pos - 1;
        buf[(size_t) pos] = buf[(size_t) (pos + n)] = in[i];
    }
    return dot (taps.data(), buf.data() + pos, n);
}

} // namespace pedwar
