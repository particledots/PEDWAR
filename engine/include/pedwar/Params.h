// PEDWAR engine: every setting the engine reads. Plain values, no plugin code.
//
// The list matches the plugin's full parameter list, which was fixed in the first build (milestone 1) so that saved
// projects never change. Since milestone 3 every setting is used.

#pragma once

namespace pedwar
{
    // The four filters: alpha, beta, gamma, delta.
    enum FilterIndex { Alpha = 0, Beta = 1, Gamma = 2, Delta = 3, kNumFilters = 4 };

    enum Mode { ModeFilter = 0, ModeRes = 1, ModeOsc = 2 };

    // What a filter sends to its stereo output. "Stereo morph" is the normal path (Morph, Morph ends and Width);
    // LP, BP, HP and Notch go to both sides alike.
    enum Tap { TapMorph = 0, TapLP = 1, TapBP = 2, TapHP = 3, TapNotch = 4, kNumTaps = 5 };

    enum MorphEnds { EndsLpHp = 0, EndsBp = 1 };
    enum BusRow { BusWidth = 0, BusFm = 1, BusAnim = 2 };
    enum PitchSplit { SplitAll = 0, SplitSplit = 1 };
    enum StereoMode { StereoSum = 0, StereoUber = 1 };

    // The plugin's default settings that Link measures the leading filter's knobs from (alpha leads beta, delta leads
    // gamma): turning the leader's knob away from its default moves the follower by the same amount.
    inline constexpr float kDefaultCoarseHz[kNumFilters] = { 110.0f, 220.0f, 440.0f, 1760.0f };
    inline constexpr float kDefaultQ = 2.0f;
    inline constexpr float kDefaultMorph = 0.5f;
    inline constexpr float kDefaultWidth = 0.35f;

    // Offset knob (-1..+1) -> semitones. S-curve: fine near the centre, big steps at the ends; full turn = +-48.
    inline float offsetSemitones (float knob) { return 48.0f * knob * knob * knob; }

    // Morph knob (0..1) -> how much of the low-pass, high-pass and band-pass outputs make the stereo morph signal.
    //   The knob reads x4 .. x2 .. x1 .. x1/2 .. x1/4 from left to right (80% of its travel). The response is the
    //   filter's notch with the notch moved to that multiple of the centre frequency: hp * a + lp * b with a = 1, b = r^2
    //   below the centre (x1/2, x1/4: leans high-pass) and a = 1/r^2, b = 1 above it (x2, x4: leans low-pass), so the
    //   pass band stays at unity. x1 is the plain symmetrical notch.
    //   The last 10% at each end fades the remaining notch away: fully left is a pure low-pass. Fully right is a pure
    //   high-pass (Morph ends LP-HP) or a pure band-pass (Morph ends BP), which is 90 degrees away from the low-pass:
    //   a self-oscillating filter shifts between sine and cosine across the knob.
    //   bpEnds: 0 = LP-HP, 1 = BP (in between while the switch cross-fades).
    struct MorphWeights { float lp, hp, bp; };
    MorphWeights morphWeights (float morph, float bpEnds);
    // The notch's multiple of the centre frequency for the Morph readout: 4 at the x4 mark, 0.25 at x1/4. Beyond the
    // marks it keeps growing / shrinking; returns 0 (fully left) or -1 (fully right) at the very ends.
    float morphMultiple (float morph);

    struct FilterParams
    {
        int mode = ModeFilter;
        float gainDb = 0.0f;         // -60..+12 dB; -60 = silent
        bool inLimit = false;        // tanh input shaper
        float coarseHz = 440.0f;     // 16..20000 Hz
        float fineCents = 0.0f;      // -100..+100 cents
        float q = 2.0f;              // 0.5..50
        bool outLimit = false;
        bool keyTrack = true;        // MIDI note moves the cutoff, relative to note 60
        int tap = TapMorph;
        bool strike = true;          // MIDI note-on pings this filter
        float levelDb = -12.0f;      // -60..+6 dB; -60 = silent

        float fmAmt = 0.0f;          // -1..+1: depth and polarity of the LFO frequency modulation (+-3 octaves)
        float morph = 0.5f;          // 0..1 (0.5 = x1, the notch)
        int morphEnds = EndsLpHp;
        float width = 0.35f;         // 0..1: stereo enhancement
    };

    struct Params
    {
        FilterParams f[kNumFilters];
        bool masterLimit = false;

        bool linkAB = false, linkGD = false;   // alpha leads beta, delta leads gamma
        float offset = 0.0f;                   // -1..+1 (S-curve, see offsetSemitones)
        int pitchSplit = SplitAll;
        float lfoRate = 0.2f;                  // Hz (the alpha LFO; the others run at fixed ratios to it)
        int bus[kNumFilters] = {};             // BusRow per LFO

        int stereoMode = StereoSum;            // sum / uber (Engine.h)
        int snap = 0, scale = 0, key = 0;      // Pitch Snap (Off / Semitones / Steps), Scale (Off + 28), Key (C..B): Pitch.h
        int quality = 1;                       // 0 = 2x, 1 = 4x, 2 = 8x oversampling
    };
} // namespace pedwar
