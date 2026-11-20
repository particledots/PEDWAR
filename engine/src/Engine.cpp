#include "pedwar/Engine.h"

#include <cstring>

namespace pedwar
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr float kMinHz = 5.0f;

    // Keeps the states inside "rails": untouched up to 3 (about +10 dB above full scale), then bent softly towards 5.
    // Only a safety net: the limiters and the Res / Osc level control normally keep the signal far below it.
    inline double rail (double x)
    {
        const double a = std::fabs (x);
        if (a <= 3.0) return x;
        const double over = a - 3.0;
        const double y = 3.0 + over / (1.0 + 0.5 * over);
        return x < 0.0 ? -y : y;
    }

    inline float onePoleCoef (double seconds, double rate) { return (float) (1.0 - std::exp (-1.0 / (seconds * rate))); }
}

Engine::Engine()
{
    for (auto& s : strikes) s.store (0);
    for (auto& h : shownHz) h.store (440.0f);
    for (auto& f : flt)
    {
        f.limL.fn = { 0.35, 0.35 };
        f.limR.fn = { 0.35, 0.35 };
    }
    masterL.fn = { 0.6, 0.4 };
    masterR.fn = { 0.6, 0.4 };
}

float Engine::targetHz (const FilterParams& fp, int note)
{
    const float kt = fp.keyTrack ? (float) (note - 60) : 0.0f;
    return fp.coarseHz * std::exp2 (fp.fineCents / 1200.0f + kt / 12.0f);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    osRate = sr * kFactor;
    for (auto& u : up) u.prepare (sr, kFactor);
    for (auto& d : dec) d.prepare (sr, kFactor);

    smoothPitch = onePoleCoef (0.003, sr);      // 3 ms: quick enough for key tracking, no zipper on automation
    smoothQ     = onePoleCoef (0.010, sr);
    smoothGain  = onePoleCoef (0.010, sr);
    rampStep    = (float) (1.0 / (0.005 * sr)); // switches cross-fade over 5 ms
    sideStep    = (float) (1.0 / (0.020 * sr)); // side chain in / out over 20 ms
    maxHz = (float) std::min (20000.0, 0.45 * sr);
    scHoldSamples = (int) (kSidechainHold * sr);
    reset();
}

void Engine::reset()
{
    updateTargets();
    for (auto& u : up) u.reset();
    for (auto& d : dec) d.reset();
    for (int i = 0; i < kNumFilters; ++i)
    {
        auto& F = flt[i];
        const auto& fp = params.f[i];
        F.ic1 = F.ic2 = F.energy = F.kick = 0.0;
        F.logHz = F.tLogHz; F.logQ = F.tLogQ; F.gain = F.tGain; F.level = F.tLevel;
        F.cLogHz = -1.0f;
        F.cLogQ = -100.0f;
        F.inLim.v = fp.inLimit ? 1.0f : 0.0f;
        F.outLim.v = fp.outLimit ? 1.0f : 0.0f;
        F.side.v = sideFeedsAll ? 1.0f : 0.0f;
        for (int t = 0; t < kNumTaps; ++t) F.tap[t].v = (t == fp.tap) ? 1.0f : 0.0f;
        F.tanhIn.unprime();
        F.limL.unprime();
        F.limR.unprime();
    }
    masterLim.v = params.masterLimit ? 1.0f : 0.0f;
    masterL.unprime();
    masterR.unprime();
    scHold = 0;
    scShown.store (false);
}

void Engine::updateTargets()
{
    for (int i = 0; i < kNumFilters; ++i)
    {
        auto& F = flt[i];
        const auto& fp = params.f[i];
        F.tLogHz = std::log2 (std::clamp (targetHz (fp, currentNote), kMinHz, maxHz));
        F.tLogQ = std::log2 (std::clamp (fp.q, 0.5f, 50.0f));
        F.tGain = dbToGain (fp.gainDb);
        F.tLevel = dbToGain (fp.levelDb);
    }
}

// ------------------------------------------------------------------------------------------------ notes

void Engine::noteOn (int note, float velocity)
{
    note = std::clamp (note, 0, 127);
    for (int i = 0; i < stackSize; ++i)
        if (noteStack[i] == note)
        {
            std::memmove (noteStack + i, noteStack + i + 1, sizeof (int) * (size_t) (stackSize - i - 1));
            --stackSize;
            break;
        }
    if (stackSize < 128) noteStack[stackSize++] = note;
    currentNote = note;                                        // last note wins
    updateTargets();

    const double v = std::clamp ((double) velocity, 0.0, 1.0);
    for (int f = 0; f < kNumFilters; ++f)
        if (params.f[f].strike)
        {
            flt[f].kick += kStrikeKick * v;
            strikes[f].fetch_add (1, std::memory_order_relaxed);
        }
}

void Engine::noteOff (int note)
{
    for (int i = 0; i < stackSize; ++i)
        if (noteStack[i] == note)
        {
            std::memmove (noteStack + i, noteStack + i + 1, sizeof (int) * (size_t) (stackSize - i - 1));
            --stackSize;
            break;
        }
    // Releasing the sounding note goes back to a note still held; with none held, the pitch stays where it was.
    if (note == currentNote && stackSize > 0)
    {
        currentNote = noteStack[stackSize - 1];
        updateTargets();
    }
}

void Engine::allNotesOff()
{
    stackSize = 0;
}

// ------------------------------------------------------------------------------------------------ audio

void Engine::renderFilter (int f, const float* tU, const float* sU, float* oL, float* oR, bool first)
{
    auto& F = flt[f];
    const auto& fp = params.f[f];

    // Controls, once per project-rate sample (smoothed). The tan() and friends only run while something moves.
    auto glide = [] (float& v, float target, float coef, float eps)
    {
        v += coef * (target - v);
        if (std::fabs (target - v) < eps) v = target;
    };
    glide (F.logHz, F.tLogHz, smoothPitch, 1.0e-6f);
    glide (F.logQ, F.tLogQ, smoothQ, 1.0e-6f);
    glide (F.gain, F.tGain, smoothGain, 1.0e-7f);
    glide (F.level, F.tLevel, smoothGain, 1.0e-7f);
    if (F.logHz != F.cLogHz)
    {
        F.g = std::tan (kPi * std::exp2 ((double) F.logHz) / osRate);
        F.cLogHz = F.logHz;
    }
    if (F.logQ != F.cLogQ)
    {
        const double Q = std::exp2 ((double) F.logQ);
        F.kq = 1.0 / Q;
        F.alpha = 0.3 * std::sqrt (0.5 / Q);
        F.cLogQ = F.logQ;
    }
    F.inLim.to (fp.inLimit ? 1.0f : 0.0f, rampStep);
    F.outLim.to (fp.outLimit ? 1.0f : 0.0f, rampStep);
    const bool useSide = sideFeedsAll || (f >= Gamma && scHold > 0);
    F.side.to (useSide ? 1.0f : 0.0f, sideStep);
    float tw[kNumTaps];
    for (int t = 0; t < kNumTaps; ++t)
    {
        F.tap[t].to (t == fp.tap ? 1.0f : 0.0f, rampStep);
        tw[t] = F.tap[t].v;
    }
    // Until milestone 2, "stereo morph" is a plain notch on both sides.
    const float wLp = tw[TapLP], wBp = tw[TapBP], wHp = tw[TapHP], wNotch = tw[TapNotch] + tw[TapMorph];

    const double g = F.g, kq = F.kq, alpha = F.alpha;
    const int mode = fp.mode;
    const double eTarget = (double) kOscLevel * (double) kOscLevel;

    // Seed a filter that is set to oscillate but holds no energy at all (it would otherwise wait for input).
    if (F.energy < 1.0e-12 && F.kick == 0.0)
    {
        if (mode == ModeOsc) F.kick = 1.0e-3;
        else if (mode == ModeRes && kq - 0.06 < 0.0) F.kick = 1.0e-6;
    }

    const float gain = F.gain, level = F.level;
    const float wIn = F.inLim.v, wOut = F.outLim.v, wSide = F.side.v;
    constexpr double tanhDrive = 1.6;

    const double kFixed = kq, a1Fixed = 1.0 / (1.0 + g * (g + kq));   // Filter mode: the same all through
    double ic1 = F.ic1, ic2 = F.ic2, E = F.energy;
    if (F.kick != 0.0) { ic1 += F.kick; F.kick = 0.0; }

    for (int s = 0; s < kFactor; ++s)
    {
        double x = (double) (tU[s] + wSide * (sU[s] - tU[s])) * gain;
        if (wIn > 0.0f)
        {
            const double shaped = F.tanhIn.next (x * tanhDrive) / tanhDrive;
            x += (double) wIn * (shaped - x);
        }
        else
            F.tanhIn.unprime();

        double k = kFixed, a1 = a1Fixed;
        if (mode != ModeFilter)
        {
            k = (mode == ModeRes) ? kq - 0.06 + 0.16 * E : -alpha * (1.0 - E / eTarget);
            k = std::clamp (k, -0.5, 4.0);
            a1 = 1.0 / (1.0 + g * (g + k));
        }
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;          // band-pass
        const double v2 = ic2 + a2 * ic1 + a3 * v3;    // low-pass
        ic1 = rail (2.0 * v1 - ic1);
        ic2 = rail (2.0 * v2 - ic2);
        E = v1 * v1 + v2 * v2;

        const double hp = x - k * v1 - v2;
        const double notch = x - k * v1;
        double y = wLp * v2 + wBp * v1 + wHp * hp + wNotch * notch;

        double yl = y, yr = y;                          // milestone 1: both sides the same
        if (wOut > 0.0f)
        {
            yl += (double) wOut * (F.limL.next (yl) - yl);
            yr += (double) wOut * (F.limR.next (yr) - yr);
        }
        else
        {
            F.limL.unprime();
            F.limR.unprime();
        }
        if (first) { oL[s] = (float) (yl * level); oR[s] = (float) (yr * level); }
        else       { oL[s] += (float) (yl * level); oR[s] += (float) (yr * level); }
    }

    // Safety: never keep a broken or vanishing state (denormals).
    if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0.0; E = 0.0; }
    if (std::fabs (ic1) < 1.0e-30) ic1 = 0.0;
    if (std::fabs (ic2) < 1.0e-30) ic2 = 0.0;
    F.ic1 = ic1;
    F.ic2 = ic2;
    F.energy = E;
}

void Engine::process (const float* track, const float* side, float* outL, float* outR, int numSamples)
{
    float tU[kFactor], sU[kFactor], oL[kFactor], oR[kFactor];
    updateTargets();

    for (int i = 0; i < numSamples; ++i)
    {
        const float tx = track != nullptr ? track[i] : 0.0f;
        const float sx = side != nullptr ? side[i] : 0.0f;
        if (std::fabs (sx) > 1.0e-6f) scHold = scHoldSamples;
        else if (scHold > 0) --scHold;

        up[0].process (tx, tU);
        up[1].process (sx, sU);

        for (int f = 0; f < kNumFilters; ++f)
            renderFilter (f, tU, sU, oL, oR, f == 0);

        masterLim.to (params.masterLimit ? 1.0f : 0.0f, rampStep);
        const float wm = masterLim.v;
        if (wm > 0.0f)
            for (int s = 0; s < kFactor; ++s)
            {
                oL[s] += wm * ((float) masterL.next (oL[s]) - oL[s]);
                oR[s] += wm * ((float) masterR.next (oR[s]) - oR[s]);
            }
        else
        {
            masterL.unprime();
            masterR.unprime();
        }

        outL[i] = dec[0].process (oL);
        outR[i] = dec[1].process (oR);
    }

    scShown.store (scHold > 0, std::memory_order_relaxed);
    for (int f = 0; f < kNumFilters; ++f)
        shownHz[f].store (std::exp2 (flt[f].logHz), std::memory_order_relaxed);
}

} // namespace pedwar
