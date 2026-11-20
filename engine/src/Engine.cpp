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

    inline float triangle (double phase) { return (float) (4.0 * std::fabs (phase - 0.5) - 1.0); }   // +1 at 0, -1 at 0.5

    constexpr float kMarks = 0.8f;      // the x4 .. x1/4 marks span 80% of the Morph knob; the last 10% each end fade out
}

MorphWeights morphWeights (float morph, float bpEnds)
{
    const float u = std::clamp (2.0f * morph - 1.0f, -1.0f, 1.0f);
    const float a = std::fabs (u);
    float w, e = 0.0f;                                   // w: the smaller of the LP / HP weights (1 / r^2)
    if (a <= kMarks) w = std::exp2 (-5.0f * a);          // r = 2^(2.5 a): x4 / x1/4 at the marks, w = 1/16 there
    else
    {
        e = (a - kMarks) / (1.0f - kMarks);
        w = (1.0f - e) / 16.0f;
    }
    if (u <= 0.0f) return { 1.0f, w, 0.0f };             // left half: the notch above the centre, leans low-pass
    // Right half: the notch below the centre, leans high-pass. With BP ends, the last stretch turns the high-pass into
    // the band-pass (equal power, so a self-oscillating filter keeps its level while it turns by 90 degrees).
    const float b = std::clamp (bpEnds, 0.0f, 1.0f);
    const float th = 1.5707963f * e;
    return { w, (1.0f - b) + b * std::cos (th), b * std::sin (th) };
}

float morphMultiple (float morph)
{
    const float u = std::clamp (2.0f * morph - 1.0f, -1.0f, 1.0f);
    const float a = std::fabs (u);
    float r;
    if (a <= kMarks) r = std::exp2 (2.5f * a);
    else
    {
        const float w = (1.0f - (a - kMarks) / (1.0f - kMarks)) / 16.0f;
        if (w < 1.0e-6f) return u < 0.0f ? 0.0f : -1.0f;
        r = 1.0f / std::sqrt (w);
    }
    return u <= 0.0f ? r : 1.0f / r;
}

Engine::Engine()
{
    for (auto& s : strikes) s.store (0);
    for (auto& h : shownHz) h.store (440.0f);
    for (auto& u : usingSide) u.store (false);
    for (auto& l : lfoShown) l.store (0.0f);
    for (auto& row : modNowShown) for (auto& m : row) m.store (0.0f);
    for (auto& row : modDepthShown) for (auto& m : row) m.store (0.0f);
    for (auto& f : flt)
    {
        f.limL.fn = { 0.35, 0.35 };
        f.limR.fn = { 0.35, 0.35 };
    }
    masterL.fn = { 0.6, 0.4 };
    masterR.fn = { 0.6, 0.4 };
}

float Engine::targetHz (const FilterParams& fp, int note, int snap, int scale, int key)
{
    const float kt = fp.keyTrack ? (float) (note - 60) : 0.0f;
    if (! pitch::active (snap, scale))
        return fp.coarseHz * std::exp2 (fp.fineCents / 1200.0f + kt / 12.0f);     // nothing snapped: as before
    // Snapped: the knobs' offset from C4 lands on the allowed notes around the filter's own note (Pitch.h).
    const int base = fp.keyTrack ? note : pitch::kBaseNote;
    const float off = pitch::quantize (pitch::offsetOf (fp.coarseHz, fp.fineCents), snap, scale, key, base);
    return pitch::hzOf (off + kt);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    // Every Quality's up- and down-sampling filters are made now (never on the audio thread), and the latency is made
    // the same for all three: the shorter ones get a small extra delay at the project rate.
    latencyAll = 0;
    int own[kNumQualities] = {};
    for (int q = 0; q < kNumQualities; ++q)
    {
        for (auto& u : up[q]) u.prepare (sr, factorFor (q));
        for (auto& d : dec[q]) d.prepare (sr, factorFor (q));
        // (the down-sampler's output sample is the last of its 'factor' oversampled ones: (factor - 1) / factor earlier)
        own[q] = (int) std::lround (up[q][0].latency() + dec[q][0].latency() - (double) (factorFor (q) - 1) / factorFor (q));
        latencyAll = std::max (latencyAll, own[q]);
    }
    for (int q = 0; q < kNumQualities; ++q) extraDelay[q] = std::min (kDelayLen - 1, latencyAll - own[q]);
    qStep = (float) (1.0 / (kQualityFade * sr));

    smoothPitch = onePoleCoef (0.003, sr);      // 3 ms: quick enough for key tracking, no zipper on automation
    smoothQ     = onePoleCoef (0.010, sr);
    smoothGain  = onePoleCoef (0.010, sr);
    smoothKnob  = onePoleCoef (0.010, sr);      // Morph, Width, FM amt
    smoothRate  = onePoleCoef (0.050, sr);
    modStep     = (float) (1.0 / (0.020 * sr)); // bus grid and Link connections cross-fade over 20 ms
    rampStep    = (float) (1.0 / (0.005 * sr)); // switches cross-fade over 5 ms
    sideStep    = (float) (1.0 / (0.020 * sr)); // side chain in / out over 20 ms
    maxHz = (float) std::min (20000.0, 0.45 * sr);
    scHoldSamples = (int) (kSidechainHold * sr);
    reset();
}

void Engine::reset()
{
    updateTargets();
    for (auto& set : up) for (auto& u : set) u.reset();
    for (auto& set : dec) for (auto& d : set) d.reset();
    setQuality (std::clamp (params.quality, 0, kNumQualities - 1));
    qPhase = 0;
    qGain = 1.0f;
    qWait = 0;
    std::fill (std::begin (delL), std::end (delL), 0.0f);
    std::fill (std::begin (delR), std::end (delR), 0.0f);
    delPos = 0;
    uber.v = params.stereoMode == StereoUber ? 1.0f : 0.0f;
    for (int i = 0; i < kNumFilters; ++i)
    {
        auto& F = flt[i];
        const auto& fp = params.f[i];
        F.ic1 = F.ic2 = F.energy = F.kick = 0.0;
        F.ap1 = F.ap2 = 0.0;
        F.logHz = F.tLogHz; F.logQ = F.tLogQ; F.gain = F.tGain; F.level = F.tLevel;
        F.morph = F.tMorph; F.width = F.tWidth; F.fm = F.tFm;
        F.cLogHz = -1.0f;
        F.cLogQ = -100.0f;
        F.cLogAp = -1.0f;
        F.cMorph = F.cBpEnds = F.cWidth = -1.0f;
        F.bpEnds.v = fp.morphEnds == EndsBp ? 1.0f : 0.0f;
        const int b = params.bus[i], prev = params.bus[(i + 1) % kNumFilters];
        F.srcAp.v = b == BusWidth ? 1.0f : 0.0f;
        F.srcFm.v = (prev == BusFm || b == BusAnim) ? 1.0f : 0.0f;
        F.srcQ.v = F.srcMorph.v = b == BusAnim ? 1.0f : 0.0f;
        F.link.v = (i == Beta && params.linkAB) || (i == Gamma && params.linkGD) ? 1.0f : 0.0f;
        F.inLim.v = fp.inLimit ? 1.0f : 0.0f;
        F.outLim.v = fp.outLimit ? 1.0f : 0.0f;
        F.side.v = sideFeedsAll ? 1.0f : 0.0f;
        for (int t = 0; t < kNumTaps; ++t) F.tap[t].v = (t == fp.tap) ? 1.0f : 0.0f;
        F.tanhIn.unprime();
        F.limL.unprime();
        F.limR.unprime();
    }
    masterLim.v = params.masterLimit ? 1.0f : 0.0f;
    // The four LFOs start a quarter cycle apart, so they never set off in step.
    for (int j = 0; j < kNumFilters; ++j) { lfoPhase[j] = 0.25 * j; lfo[j] = triangle (lfoPhase[j]); }
    logRate = std::log2 (std::clamp (params.lfoRate, 0.01f, 20.0f));
    masterL.unprime();
    masterR.unprime();
    scHold = 0;
    trackHold = 0;
    scShown.store (false);
    for (auto& u : usingSide) u.store (sideFeedsAll, std::memory_order_relaxed);
}

int Engine::noteFor (int f) const
{
    if (params.pitchSplit == SplitSplit) return f < Gamma ? noteHigh : noteLow;
    return noteAll;
}

void Engine::setQuality (int q)
{
    qActive = q;
    fac = factorFor (q);
    osRate = sr * fac;
    for (auto& d : dec[q]) d.reset();
    for (auto& F : flt)          // the filters' coefficients are worked out again for the new rate
    {
        F.cLogHz = -1.0f;
        F.cLogQ = -100.0f;
        F.cLogAp = -1.0f;
    }
}

void Engine::updateTargets()
{
    const float offsetOct = offsetSemitones (std::clamp (params.offset, -1.0f, 1.0f)) / 12.0f;
    const int snap = params.snap, scale = params.scale, key = params.key;
    const bool snapping = pitch::active (snap, scale);
    float lc[kNumFilters], lq[kNumFilters], knobOct[kNumFilters];
    for (int i = 0; i < kNumFilters; ++i)
    {
        auto& F = flt[i];
        const auto& fp = params.f[i];
        const int note = noteFor (i);
        const float hz = targetHz (fp, note, snap, scale, key);
        lc[i] = std::log2 (std::max (1.0f, hz)) + offsetOct;
        // the knobs' own (snapped) tuning, for Link
        knobOct[i] = snapping ? std::log2 (std::max (1.0f, hz)) - (fp.keyTrack ? (float) (note - 60) / 12.0f : 0.0f) : 0.0f;
        lq[i] = std::log2 (std::clamp (fp.q, 0.5f, 50.0f));
        F.tMorph = fp.morph;
        F.tWidth = fp.width;
        F.tFm = fp.fmAmt;
        F.tGain = dbToGain (fp.gainDb);
        F.tLevel = dbToGain (fp.levelDb);
    }
    // Link: the follower adds the leader's knobs, measured from the leader's defaults.
    for (int pair = 0; pair < 2; ++pair)
    {
        const bool on = pair == 0 ? params.linkAB : params.linkGD;
        if (! on) continue;
        const int lead = pair == 0 ? Alpha : Delta, fol = pair == 0 ? Beta : Gamma;
        const auto& L = params.f[lead];
        if (snapping) lc[fol] += knobOct[lead] - std::log2 (kDefaultCoarseHz[lead]);     // the leader's snapped tuning
        else          lc[fol] += std::log2 (std::max (1.0f, L.coarseHz) / kDefaultCoarseHz[lead]) + L.fineCents / 1200.0f;
        lq[fol] += std::log2 (std::clamp (L.q, 0.5f, 50.0f) / kDefaultQ);
        auto& F = flt[fol];
        F.tFm += L.fmAmt;
        F.tWidth += L.width - kDefaultWidth;
        F.tMorph += L.morph - kDefaultMorph;
    }
    for (int i = 0; i < kNumFilters; ++i)
    {
        auto& F = flt[i];
        F.tLogHz = std::clamp (lc[i], std::log2 (kMinHz), std::log2 (maxHz));
        F.tLogQ = std::clamp (lq[i], -1.0f, std::log2 (50.0f));
        F.tMorph = std::clamp (F.tMorph, 0.0f, 1.0f);
        F.tWidth = std::clamp (F.tWidth, 0.0f, 1.0f);
        F.tFm = std::clamp (F.tFm, -1.0f, 1.0f);
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
    noteAll = note;                                            // last note wins
    (note >= 60 ? noteHigh : noteLow) = note;                  // Pitch gamma-delta "split": each half its own last note
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
    // The same for each half of the keyboard (split).
    auto lastHeld = [this] (bool high, int fallback)
    {
        for (int i = stackSize - 1; i >= 0; --i)
            if ((noteStack[i] >= 60) == high) return noteStack[i];
        return fallback;
    };
    bool changed = false;
    if (note == noteAll && stackSize > 0) { noteAll = noteStack[stackSize - 1]; changed = true; }
    if (note == noteHigh) { noteHigh = lastHeld (true, noteHigh); changed = true; }
    if (note == noteLow)  { noteLow = lastHeld (false, noteLow); changed = true; }
    if (changed) updateTargets();
}

void Engine::allNotesOff()
{
    stackSize = 0;
}

// ------------------------------------------------------------------------------------------------ LFOs

void Engine::stepLfos()
{
    logRate += smoothRate * (std::log2 (std::clamp (params.lfoRate, 0.01f, 20.0f)) - logRate);
    const double base = std::exp2 ((double) logRate) / sr;
    for (int j = 0; j < kNumFilters; ++j)
    {
        // anim: the next LFO nudges this one's speed (the same switch that animates filter j)
        const float nudge = flt[j].srcQ.v * kAnimRateOctaves * lfo[(j + 1) % kNumFilters];
        double ph = lfoPhase[j] + base * (double) kLfoRatio[j] * (nudge != 0.0f ? (double) std::exp2 (nudge) : 1.0);
        ph -= std::floor (ph);
        lfoPhase[j] = ph;
    }
    for (int j = 0; j < kNumFilters; ++j) lfo[j] = triangle (lfoPhase[j]);
}

void Engine::computeMods (Mod* mods)
{
    for (int j = 0; j < kNumFilters; ++j)
    {
        auto& F = flt[j];
        const int b = params.bus[j], next = (j + 1) % kNumFilters;
        F.srcAp.to (b == BusWidth ? 1.0f : 0.0f, modStep);
        F.srcFm.to ((params.bus[next] == BusFm || b == BusAnim) ? 1.0f : 0.0f, modStep);
        F.srcQ.to (b == BusAnim ? 1.0f : 0.0f, modStep);
        F.srcMorph.to (b == BusAnim ? 1.0f : 0.0f, modStep);
        F.link.to ((j == Beta && params.linkAB) || (j == Gamma && params.linkGD) ? 1.0f : 0.0f, modStep);

        auto& m = mods[j];
        const float fmDepth = F.srcFm.v * F.fm * kFmOctaves;
        m.pitchOct = fmDepth * lfo[next];
        m.qOct = F.srcQ.v * kAnimQOctaves * lfo[(j + 2) % kNumFilters];
        m.morph = F.srcMorph.v * kAnimMorph * lfo[(j + 3) % kNumFilters];
        m.ap = F.srcAp.v * kApSweepOctaves * lfo[j];
        m.dPitch = std::fabs (fmDepth);
        m.dQ = F.srcQ.v * kAnimQOctaves;
        m.dMorph = F.srcMorph.v * kAnimMorph;
    }
    // Link passes the leader's LFO movement of cutoff, Q and Morph on to the follower.
    for (int pair = 0; pair < 2; ++pair)
    {
        const int lead = pair == 0 ? Alpha : Delta, fol = pair == 0 ? Beta : Gamma;
        const float k = flt[fol].link.v;
        if (k <= 0.0f) continue;
        auto& M = mods[fol];
        const auto& L = mods[lead];
        M.pitchOct += k * L.pitchOct;  M.dPitch += k * L.dPitch;
        M.qOct += k * L.qOct;          M.dQ += k * L.dQ;
        M.morph += k * L.morph;        M.dMorph += k * L.dMorph;
    }
}

// ------------------------------------------------------------------------------------------------ audio

void Engine::renderFilter (int f, const Mod& md, const float* tU, const float* sU, float* oL, float* oR, float* uL, float* uR,
                           bool first)
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
    glide (F.morph, F.tMorph, smoothKnob, 1.0e-6f);
    glide (F.width, F.tWidth, smoothKnob, 1.0e-6f);
    glide (F.fm, F.tFm, smoothKnob, 1.0e-6f);

    const float lo = std::log2 (kMinHz), hi = std::log2 (maxHz);
    const float logHz = std::clamp (F.logHz + md.pitchOct, lo, hi);
    const float logQ = std::clamp (F.logQ + md.qOct, -1.0f, std::log2 (50.0f));
    if (logHz != F.cLogHz)
    {
        F.g = std::tan (kPi * std::exp2 ((double) logHz) / osRate);
        F.cLogHz = logHz;
    }
    if (logQ != F.cLogQ)
    {
        const double Q = std::exp2 ((double) logQ);
        F.kq = 1.0 / Q;
        F.alpha = 0.3 * std::sqrt (0.5 / Q);
        F.cLogQ = logQ;
    }
    // The width all-pass: corner at the filter's frequency, swept by the LFO (kept well inside the oversampled range).
    const float logAp = std::clamp (logHz + md.ap, lo, (float) std::log2 (0.4 * osRate));
    if (logAp != F.cLogAp)
    {
        const double ga = std::tan (kPi * std::exp2 ((double) logAp) / osRate);
        F.apG = ga / (1.0 + ga);
        F.cLogAp = logAp;
    }

    F.inLim.to (fp.inLimit ? 1.0f : 0.0f, rampStep);
    F.outLim.to (fp.outLimit ? 1.0f : 0.0f, rampStep);
    F.bpEnds.to (fp.morphEnds == EndsBp ? 1.0f : 0.0f, rampStep);
    // gamma and delta take the side chain while it carries audio. alpha and beta take it too while the track input
    // has been silent for 2 s: that is how PEDWAR gets its audio in the instrument slot of a Software Instrument track
    // (Logic's MIDI-controlled effect set-up), where the side chain is the only audio there is.
    const bool useSide = sideFeedsAll || (scHold > 0 && (f >= Gamma || trackHold == 0));
    F.side.to (useSide ? 1.0f : 0.0f, sideStep);
    float tw[kNumTaps];
    for (int t = 0; t < kNumTaps; ++t)
    {
        F.tap[t].to (t == fp.tap ? 1.0f : 0.0f, rampStep);
        tw[t] = F.tap[t].v;
    }
    const double wLp = tw[TapLP], wBp = tw[TapBP], wHp = tw[TapHP], wNotch = tw[TapNotch], wMorph = tw[TapMorph];

    // Stereo morph: the moved notch, and the width blend on one side (right on alpha / beta, left on gamma / delta).
    const float morphNow = std::clamp (F.morph + md.morph, 0.0f, 1.0f);
    if (morphNow != F.cMorph || F.bpEnds.v != F.cBpEnds)
    {
        F.mw = morphWeights (morphNow, F.bpEnds.v);
        F.cMorph = morphNow;
        F.cBpEnds = F.bpEnds.v;
    }
    if (F.width != F.cWidth)
    {
        const double theta = 1.5707963267948966 * (double) F.width;
        F.wDry = std::cos (theta);
        F.wAp = std::sin (theta);
        F.cWidth = F.width;
    }
    const double mLp = F.mw.lp, mHp = F.mw.hp, mBp = F.mw.bp;
    const double wDry = F.wDry, wAp = F.wAp;
    const bool blendRight = f < Gamma;
    const double apG = F.apG;

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
    double ic1 = F.ic1, ic2 = F.ic2, E = F.energy, ap1 = F.ap1, ap2 = F.ap2;
    if (F.kick != 0.0) { ic1 += F.kick; F.kick = 0.0; }

    const int n = fac;
    for (int s = 0; s < n; ++s)
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
        const double mono = wLp * v2 + wBp * v1 + wHp * hp + wNotch * notch;

        // stereo morph, and its all-pass copy (two first-order stages, trapezoidal)
        const double morph = mLp * v2 + mHp * hp + mBp * v1;
        double vv = (morph - ap1) * apG;
        const double lp1 = vv + ap1;
        ap1 = lp1 + vv;
        const double y1 = 2.0 * lp1 - morph;
        vv = (y1 - ap2) * apG;
        const double lp2 = vv + ap2;
        ap2 = lp2 + vv;
        const double shifted = 2.0 * lp2 - y1;
        const double wide = wDry * morph + wAp * shifted;

        double yl = mono + wMorph * (blendRight ? morph : wide);
        double yr = mono + wMorph * (blendRight ? wide : morph);
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
        const float L = (float) (yl * level), R = (float) (yr * level);
        // uber: alpha's and beta's right sides go left, gamma's and delta's left sides go right
        if (first) { oL[s] = L; oR[s] = R; uL[s] = R; uR[s] = 0.0f; }
        else
        {
            oL[s] += L; oR[s] += R;
            if (blendRight) uL[s] += R; else uR[s] += L;
        }
    }

    // Safety: never keep a broken or vanishing state (denormals).
    if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0.0; E = 0.0; }
    if (! std::isfinite (ap1) || ! std::isfinite (ap2)) ap1 = ap2 = 0.0;
    if (std::fabs (ic1) < 1.0e-30) ic1 = 0.0;
    if (std::fabs (ic2) < 1.0e-30) ic2 = 0.0;
    if (std::fabs (ap1) < 1.0e-30) ap1 = 0.0;
    if (std::fabs (ap2) < 1.0e-30) ap2 = 0.0;
    F.ic1 = ic1;
    F.ic2 = ic2;
    F.energy = E;
    F.ap1 = ap1;
    F.ap2 = ap2;
}

void Engine::process (const float* track, const float* side, float* outL, float* outR, int numSamples)
{
    float tU[kMaxFactor], sU[kMaxFactor], oL[kMaxFactor], oR[kMaxFactor], uL[kMaxFactor], uR[kMaxFactor];
    Mod mods[kNumFilters];
    updateTargets();
    const int wantQ = std::clamp (params.quality, 0, kNumQualities - 1);
    const float uberTarget = params.stereoMode == StereoUber ? 1.0f : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float tx = track != nullptr ? track[i] : 0.0f;
        const float sx = side != nullptr ? side[i] : 0.0f;
        if (std::fabs (sx) > 1.0e-6f) scHold = scHoldSamples;
        else if (scHold > 0) --scHold;
        if (std::fabs (tx) > 1.0e-6f) trackHold = scHoldSamples;
        else if (trackHold > 0) --trackHold;

        // The Quality running now does the work; the other two only keep their input memory filled.
        for (int q = 0; q < kNumQualities; ++q)
        {
            if (q == qActive) { up[q][0].process (tx, tU); up[q][1].process (sx, sU); }
            else              { up[q][0].push (tx);        up[q][1].push (sx); }
        }

        stepLfos();
        computeMods (mods);
        for (int f = 0; f < kNumFilters; ++f)
            renderFilter (f, mods[f], tU, sU, oL, oR, uL, uR, f == 0);

        // Stereo sum / uber (cross-faded over 20 ms)
        uber.to (uberTarget, modStep);
        if (uber.v > 0.0f)
        {
            const float w = uber.v;
            for (int s = 0; s < fac; ++s)
            {
                oL[s] += w * (kUberGain * uL[s] - oL[s]);
                oR[s] += w * (kUberGain * uR[s] - oR[s]);
            }
        }

        masterLim.to (params.masterLimit ? 1.0f : 0.0f, rampStep);
        const float wm = masterLim.v;
        if (wm > 0.0f)
            for (int s = 0; s < fac; ++s)
            {
                oL[s] += wm * ((float) masterL.next (oL[s]) - oL[s]);
                oR[s] += wm * ((float) masterR.next (oR[s]) - oR[s]);
            }
        else
        {
            masterL.unprime();
            masterR.unprime();
        }

        float yl = dec[qActive][0].process (oL);
        float yr = dec[qActive][1].process (oR);

        // Quality change: fade out, switch, let the new down-sampling filter fill, fade in.
        if (qPhase != 0 || wantQ != qActive)
        {
            const float sm = qGain * qGain * (3.0f - 2.0f * qGain);     // smooth S-shaped fade
            yl *= sm;
            yr *= sm;
            if (qPhase == 0) qPhase = 1;
            if (qPhase == 1)
            {
                qGain -= qStep;
                if (qGain <= 0.0f)
                {
                    qGain = 0.0f;
                    setQuality (wantQ);
                    qWait = (int) std::ceil (2.0 * dec[qActive][0].latency()) + 2;
                    qPhase = 2;
                }
            }
            else if (qPhase == 2) { if (--qWait <= 0) qPhase = 3; }
            else if (qPhase == 3)
            {
                if (wantQ != qActive) qPhase = 1;
                else if ((qGain += qStep) >= 1.0f) { qGain = 1.0f; qPhase = 0; }
            }
        }

        // The extra delay that makes the latency the same at every Quality.
        delL[delPos] = yl;
        delR[delPos] = yr;
        const int rd = (delPos - extraDelay[qActive] + kDelayLen) % kDelayLen;
        outL[i] = delL[rd];
        outR[i] = delR[rd];
        delPos = (delPos + 1) % kDelayLen;
    }

    scShown.store (scHold > 0, std::memory_order_relaxed);
    for (int f = 0; f < kNumFilters; ++f)
    {
        usingSide[f].store (sideFeedsAll || (scHold > 0 && (f >= Gamma || trackHold == 0)), std::memory_order_relaxed);
        shownHz[f].store (std::exp2 (std::clamp (flt[f].logHz + (numSamples > 0 ? mods[f].pitchOct : 0.0f),
                                                 std::log2 (kMinHz), std::log2 (maxHz))), std::memory_order_relaxed);
        lfoShown[f].store (lfo[f], std::memory_order_relaxed);
        if (numSamples > 0)
        {
            const auto& m = mods[f];
            modNowShown[f][ModCoarse].store (m.pitchOct, std::memory_order_relaxed);
            modNowShown[f][ModQ].store (m.qOct, std::memory_order_relaxed);
            modNowShown[f][ModMorph].store (m.morph, std::memory_order_relaxed);
            modDepthShown[f][ModCoarse].store (m.dPitch, std::memory_order_relaxed);
            modDepthShown[f][ModQ].store (m.dQ, std::memory_order_relaxed);
            modDepthShown[f][ModMorph].store (m.dMorph, std::memory_order_relaxed);
        }
    }
}

} // namespace pedwar
