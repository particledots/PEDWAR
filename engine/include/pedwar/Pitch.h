// PEDWAR engine: Pitch Snap, Scale and Key. Header only, no dependencies.
//
// The same quantizer as COMPLICATOR (engine/include/complicator/Pitch.h), with the range widened to PEDWAR's Coarse knob.
// It acts on each filter's own tuning, Coarse and Fine together. Key tracking already works relative to middle C
// (MIDI note 60), so the knobs' tuning is read as an offset in semitones from C4 (Coarse at C4 = 261.6 Hz, Fine 0 is
// offset 0: the filter plays the note you play). Offset, Link (on top of the leader's snapped tuning) and the LFOs are
// added afterwards and are never snapped.
//
//   Snap Off        the offset is used as it is (smooth).
//   Snap Semitones  the offset lands on whole semitones.
//   Snap Steps      the offset lands on octaves and fifths: 0, +-7, +-12, +-19, +-24 ... (as in COMPLICATOR and Ceo).
//
//   Scale (Off + 28) and Key (C..B): with a scale on, the offset only lands where (the filter's own note + offset) is a
//   note of that scale in that key. The filter's own note is the note you play when its Key track is on, otherwise C4.
//   That note itself and its octaves are always allowed, so Coarse at C4 always plays exactly the note you played.
//   With a scale on and Snap Off, the offset still moves in whole notes (a scale has no "between" notes).

#pragma once
#include "Scales.h"
#include <cmath>
#include <cstdlib>

namespace pedwar
{
namespace pitch
{
    enum Snap { SnapOff = 0, SnapSemitones = 1, SnapSteps = 2 };

    constexpr int kBaseNote = 60;               // key tracking and the knobs' offset are relative to middle C
    constexpr int kLowest = -49, kHighest = 76;  // Coarse 16 Hz .. 20 kHz (with Fine) = about -48.3 .. +75.2 semitones
    constexpr int kSteps[] = { -48, -43, -36, -31, -24, -19, -12, -7, 0, 7, 12, 19, 24, 31, 36, 43, 48, 55, 60, 67, 72 };

    inline int wrap12 (int n) { n %= 12; return n < 0 ? n + 12 : n; }

    inline bool active (int snap, int scaleMenu) { return snap == SnapSemitones || snap == SnapSteps || scales::maskFor (scaleMenu) != 0; }

    // True when 'offset' (whole semitones) is allowed above 'baseNote' for this scale mask and key.
    inline bool allowed (int offset, int baseNote, int mask, int key)
    {
        if (mask == 0 || wrap12 (offset) == 0) return true;            // no scale, or the note itself / its octaves
        return ((mask >> wrap12 (baseNote + offset - key)) & 1) != 0;
    }

    // The offset (semitones from C4) actually played for a tuning of 'offset' (fractional).
    inline float quantize (float offset, int snap, int scaleMenu, int key, int baseNote)
    {
        const int mask = scales::maskFor (scaleMenu);
        if (snap != SnapSemitones && snap != SnapSteps && mask == 0) return offset;
        int best = 0;
        float bestDist = 1.0e9f;
        auto consider = [&] (int o)
        {
            if (! allowed (o, baseNote, mask, key)) return;
            const float dist = std::fabs ((float) o - offset);
            // Ties go to the offset nearer to 0, so the result never depends on the search order.
            if (dist < bestDist - 1.0e-6f || (std::fabs (dist - bestDist) <= 1.0e-6f && std::abs (o) < std::abs (best)))
            { best = o; bestDist = dist; }
        };
        if (snap == SnapSteps)
            for (int o : kSteps) consider (o);
        else
            for (int o = kLowest; o <= kHighest; ++o) consider (o);
        return (float) best;
    }

    // Coarse (Hz) and Fine (cents) as an offset in semitones from C4, and back.
    inline float offsetOf (float coarseHz, float fineCents)
    {
        return 12.0f * std::log2 ((coarseHz > 1.0f ? coarseHz : 1.0f) / 261.62556f) + fineCents / 100.0f;
    }
    inline float hzOf (float offset) { return 261.62556f * std::exp2 (offset / 12.0f); }
} // namespace pitch
} // namespace pedwar
