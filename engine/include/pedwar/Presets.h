// PEDWAR engine: the factory presets. Plain C++, no plugin code. Header only.
//
// All of them are my own settings (the ceithir manual has no example patches). They are only ever added at the END of
// the list: the Preset menu's choice is saved with a project as a number, so the order of existing entries must never
// change.
//
// A preset sets everything that makes the sound (every filter, Link, Offset, Pitch gamma-delta, Rate, the bus grid,
// Stereo and Master limit). It never changes Pitch Snap, Scale, Key or Quality: those are the player's own set-up.
//
// Level matching: each preset has a 'trim' (dB) added to all four Levels. The trims were set so every preset is about
// as loud as Init (K-weighted RMS, the measure behind LUFS): an "input" preset with pink noise going in, a "voice"
// preset with no input, played by notes (a held note, or a note every quarter second for the struck ones).
// The engine self-test measures every preset and fails if one is more than 1.5 dB away.

#pragma once
#include "Params.h"
#include <algorithm>

namespace pedwar
{
namespace presets
{
    enum Kind { Input = 0, Voice = 1, Struck = 2 };   // what the preset needs to sound: audio in, a held note, struck notes

    struct Info { const char* name; Kind kind; const char* playWith; float trim; };

    constexpr int kNumFactory = 14;

    inline const Info& info (int i)
    {
        static const Info list[kNumFactory] = {
            { "Notch Phaser",        Input,  "any audio: four notches drift slowly",                    0.0f },
            { "Vowel Ah",            Input,  "noise, drums, synths: four formant band-passes",          0.0f },
            { "Talking Formants",    Input,  "any audio: the formants wander (anim)",                   0.0f },
            { "Uber Space",          Input,  "any audio: wide, moving stereo (uber)",                   0.0f },
            { "Tanh Low Pass",       Input,  "bass, drums: driven low-pass",                            0.0f },
            { "Auto Wah",            Input,  "guitar, keys, drums: two LFO-swept band-passes",          0.0f },
            { "Sympathetic Strings", Input,  "any audio plus notes: rings at the note and its harmonics", 0.0f },
            { "Sine Drone",          Voice,  "a held note: four sines on the note's harmonics",         0.0f },
            { "Struck Bell",         Struck, "notes C4 and up: bell partials, velocity sets the hit",   0.0f },
            { "Mallets",             Struck, "notes around C4 to C6: marimba-like",                     0.0f },
            { "Tom Cluster",         Struck, "low to middle notes: four tuned drums",                   0.0f },
            { "Singing Res",         Voice,  "a held note: self-oscillating, slowly animated",          0.0f },
            { "Split Drone",         Voice,  "one note below middle C and one above: two drones",       0.0f },
            { "Bleep Swarm",         Voice,  "a held note: four sines frequency-modulating fast",       0.0f },
        };
        static const Info none { "", Input, "", 0.0f };
        return (i >= 0 && i < kNumFactory) ? list[i] : none;
    }
    inline const char* name (int i) { return info (i).name; }

    // What the plugin's Init program sets: every control at its default value (the defaults in Parameters.cpp).
    inline Params init()
    {
        Params p;
        for (int f = 0; f < kNumFilters; ++f) p.f[f].coarseHz = kDefaultCoarseHz[f];
        return p;
    }

    // Notes, for tuning (Hz)
    constexpr float C2 = 65.40639f, G2 = 97.99886f, C3 = 130.8128f, G3 = 195.9977f, C4 = 261.6256f, E4 = 329.6276f,
                    G4 = 391.9954f, C5 = 523.2511f, G5 = 783.9909f, C6 = 1046.502f;

    // The preset before its level trim.
    inline Params untrimmed (int i)
    {
        Params p = init();
        auto& a = p.f[Alpha]; auto& b = p.f[Beta]; auto& g = p.f[Gamma]; auto& d = p.f[Delta];
        auto all = [&p] (auto fn) { for (auto& f : p.f) fn (f); };
        auto tune = [&p] (float x0, float x1, float x2, float x3) { p.f[0].coarseHz = x0; p.f[1].coarseHz = x1; p.f[2].coarseHz = x2; p.f[3].coarseHz = x3; };
        auto qs = [&p] (float x0, float x1, float x2, float x3) { p.f[0].q = x0; p.f[1].q = x1; p.f[2].q = x2; p.f[3].q = x3; };
        auto levels = [&p] (float x0, float x1, float x2, float x3) { p.f[0].levelDb = x0; p.f[1].levelDb = x1; p.f[2].levelDb = x2; p.f[3].levelDb = x3; };
        auto buses = [&p] (int x0, int x1, int x2, int x3) { p.bus[0] = x0; p.bus[1] = x1; p.bus[2] = x2; p.bus[3] = x3; };
        constexpr float off = -60.0f;

        switch (i)
        {
            case 0:   // Notch Phaser: four notches, each slowly moved by another filter's LFO (FM bus)
                all ([] (FilterParams& f) { f.tap = TapMorph; f.q = 0.9f; f.fmAmt = 0.35f; f.width = 0.5f; f.keyTrack = false; f.strike = false; });
                tune (300.0f, 750.0f, 1700.0f, 3800.0f);
                levels (-10.5f, -10.5f, -10.5f, -10.5f);
                buses (BusFm, BusFm, BusFm, BusFm);
                p.lfoRate = 0.12f;
                break;

            case 1:   // Vowel Ah: four band-passes on the formants of "ah"
                all ([] (FilterParams& f) { f.tap = TapBP; f.keyTrack = false; f.strike = false; });
                tune (730.0f, 1090.0f, 2440.0f, 3400.0f);
                qs (5.0f, 7.0f, 10.0f, 12.0f);
                levels (-4.0f, -7.0f, -12.0f, -14.0f);
                break;

            case 2:   // Talking Formants: formant band-passes (Morph near the BP end), Q and Morph animated
                all ([] (FilterParams& f) { f.tap = TapMorph; f.morphEnds = EndsBp; f.morph = 0.92f; f.width = 0.5f;
                                            f.fmAmt = 0.2f; f.keyTrack = false; f.strike = false; });
                tune (600.0f, 1150.0f, 2400.0f, 3300.0f);
                qs (6.0f, 8.0f, 10.0f, 10.0f);
                levels (-6.5f, -8.5f, -12.5f, -14.5f);
                buses (BusAnim, BusAnim, BusAnim, BusAnim);
                p.lfoRate = 0.35f;
                break;

            case 3:   // Uber Space: leaning notches at full width, swept by their own LFOs, uber stereo
                all ([] (FilterParams& f) { f.tap = TapMorph; f.q = 0.7f; f.width = 1.0f; f.keyTrack = false; f.strike = false; });
                tune (180.0f, 650.0f, 2000.0f, 6000.0f);
                a.morph = 0.35f; b.morph = 0.42f; g.morph = 0.58f; d.morph = 0.65f;
                levels (-2.5f, -2.5f, -2.5f, -2.5f);
                p.lfoRate = 0.18f;
                p.stereoMode = StereoUber;
                break;

            case 4:   // Tanh Low Pass: two driven low-passes an octave apart
                all ([] (FilterParams& f) { f.tap = TapLP; f.keyTrack = false; f.strike = false; });
                a.coarseHz = 800.0f;  a.q = 1.6f; a.gainDb = 9.0f; a.inLimit = true; a.outLimit = true;
                b.coarseHz = 1600.0f; b.q = 1.2f; b.gainDb = 6.0f; b.inLimit = true; b.outLimit = true;
                levels (-5.5f, -11.5f, off, off);
                break;

            case 5:   // Auto Wah: alpha moved by beta's LFO, delta by alpha's (opposite way), both band-pass
                all ([] (FilterParams& f) { f.tap = TapBP; f.q = 4.0f; f.keyTrack = false; f.strike = false; f.outLimit = true; });
                a.coarseHz = 500.0f;  a.fmAmt = 0.45f;
                d.coarseHz = 1100.0f; d.fmAmt = -0.45f;
                levels (-1.0f, off, off, -3.0f);
                buses (BusFm, BusFm, BusWidth, BusWidth);
                p.lfoRate = 2.2f;
                break;

            case 6:   // Sympathetic Strings: four high-Q band-passes on the note and its harmonics; notes also pluck them
                all ([] (FilterParams& f) { f.mode = ModeFilter; f.tap = TapBP; f.q = 45.0f; f.gainDb = -6.0f; f.outLimit = true; });
                tune (C4, C5, G5, C6);
                levels (-5.0f, -8.0f, -11.0f, -13.0f);
                break;

            case 7:   // Sine Drone: four steady sines on the note's harmonics (1, 2, 3, 4)
                all ([] (FilterParams& f) { f.mode = ModeOsc; f.tap = TapLP; f.strike = false; });
                tune (C3, C4, G4, C5);
                levels (-10.5f, -13.5f, -18.5f, -20.5f);
                break;

            case 8:   // Struck Bell: ringing band-passes on bell partials (1, 2.76, 5.40, 8.93)
                all ([] (FilterParams& f) { f.tap = TapBP; f.q = 50.0f; });
                tune (C4, C4 * 2.76f, C4 * 5.40f, C4 * 8.93f);
                levels (-1.0f, -5.0f, -9.0f, -14.0f);
                p.masterLimit = true;
                break;

            case 9:   // Mallets: three shorter rings on marimba-like partials (1, 3.93, 9.2)
                all ([] (FilterParams& f) { f.tap = TapBP; f.q = 25.0f; });
                a.q = 40.0f;
                tune (C4, C4 * 3.93f, C4 * 9.2f, 1760.0f);
                levels (0.5f, -7.5f, -13.5f, off);
                d.strike = false;
                p.masterLimit = true;
                break;

            case 10:  // Tom Cluster: four low drums
                all ([] (FilterParams& f) { f.tap = TapBP; f.q = 28.0f; });
                tune (70.0f, 105.0f, 160.0f, 240.0f);
                levels (-4.5f, -5.5f, -7.5f, -10.5f);
                p.masterLimit = true;
                break;

            case 11:  // Singing Res: Res mode past its singing point, Q and Morph slowly animated
                all ([] (FilterParams& f) { f.mode = ModeRes; f.q = 30.0f; f.tap = TapMorph; f.morphEnds = EndsBp; f.morph = 0.9f;
                                            f.width = 0.6f; f.fmAmt = 0.03f; f.strike = false; });
                tune (C3, G3, C4, E4);
                levels (-5.0f, -7.0f, -9.0f, -11.0f);
                buses (BusAnim, BusAnim, BusAnim, BusAnim);
                p.lfoRate = 0.25f;
                break;

            case 12:  // Split Drone: notes from middle C up play alpha and beta, notes below play gamma and delta
                all ([] (FilterParams& f) { f.mode = ModeOsc; f.tap = TapLP; f.strike = false; });
                tune (C4, G4, C3, G2);
                levels (-12.5f, -16.5f, -12.5f, -16.5f);
                p.pitchSplit = SplitSplit;
                break;

            case 13:  // Bleep Swarm: four sines, each frequency-modulated fast by another one's LFO, uber stereo
                all ([] (FilterParams& f) { f.mode = ModeOsc; f.tap = TapMorph; f.morph = 0.15f; f.width = 0.8f; f.strike = false; });
                tune (300.0f, 450.0f, 600.0f, 900.0f);
                a.fmAmt = 0.5f; b.fmAmt = -0.5f; g.fmAmt = 0.4f; d.fmAmt = -0.4f;
                levels (-10.5f, -10.5f, -10.5f, -10.5f);
                buses (BusFm, BusFm, BusFm, BusFm);
                p.lfoRate = 6.0f;
                p.stereoMode = StereoUber;
                break;

            default:
                break;
        }
        return p;
    }

    // The preset as the Preset menu sets it: the trim added to every Level that is not off.
    inline Params factory (int i)
    {
        Params p = untrimmed (i);
        const float t = info (i).trim;
        for (auto& f : p.f)
            if (f.levelDb > -59.95f) f.levelDb = std::min (6.0f, std::max (-59.0f, f.levelDb + t));
        return p;
    }
} // namespace presets
} // namespace pedwar
