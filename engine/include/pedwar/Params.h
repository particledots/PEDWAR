// PEDWAR engine: every setting the engine reads. Plain values, no plugin code.
//
// The list matches the plugin's full parameter list, which was fixed in the first build (milestone 1) so that saved
// projects never change. Settings marked "M2" or "M3" are stored but not used yet.

#pragma once

namespace pedwar
{
    // The four filters: alpha, beta, gamma, delta.
    enum FilterIndex { Alpha = 0, Beta = 1, Gamma = 2, Delta = 3, kNumFilters = 4 };

    enum Mode { ModeFilter = 0, ModeRes = 1, ModeOsc = 2 };

    // What a filter sends to its stereo output. "Stereo morph" is the normal path (a plain notch on both sides until
    // milestone 2).
    enum Tap { TapMorph = 0, TapLP = 1, TapBP = 2, TapHP = 3, TapNotch = 4, kNumTaps = 5 };

    enum MorphEnds { EndsLpHp = 0, EndsBp = 1 };          // M2
    enum BusRow { BusWidth = 0, BusFm = 1, BusAnim = 2 };  // M2
    enum PitchSplit { SplitAll = 0, SplitSplit = 1 };      // M2
    enum StereoMode { StereoSum = 0, StereoUber = 1 };     // M3

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

        float fmAmt = 0.0f;          // M2: -1..+1
        float morph = 0.5f;          // M2: 0..1 (0.5 = x1)
        int morphEnds = EndsLpHp;    // M2
        float width = 0.35f;         // M2: 0..1
    };

    struct Params
    {
        FilterParams f[kNumFilters];
        bool masterLimit = false;

        bool linkAB = false, linkGD = false;   // M2
        float offset = 0.0f;                   // M2: -1..+1 (S-curve)
        int pitchSplit = SplitAll;             // M2
        float lfoRate = 0.2f;                  // M2: Hz
        int bus[kNumFilters] = {};             // M2: BusRow per LFO

        int stereoMode = StereoSum;            // M3
        int snap = 0, scale = 0, key = 0;      // M3: Pitch Snap (Off / Semitones / Steps), Scale (Off + 28), Key (C..B)
        int quality = 1;                       // M3: 0 = 2x, 1 = 4x, 2 = 8x (fixed at 4x until then)
    };
} // namespace pedwar
