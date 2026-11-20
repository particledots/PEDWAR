// Every control of PEDWAR and PEDWAR Synth (the same list in both), as Logic sees it.
//
// IMPORTANT: these ids, their order, ranges and menu lists were fixed in the first build (milestone 1) and must never
// change. Saved projects and automation refer to them. Controls for later milestones are already here (they are
// stored, and shown dimmed on the panel) so nothing has to be added in between.
// If something must be added one day, add it at the END with ParameterID version 2.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace PdParams
{
    inline constexpr int numFilters = 4;

    // Per filter: the id is the filter's letter (a = alpha, b = beta, g = gamma, d = delta) + one of these.
    enum Slot
    {
        Mode, Gain, InLimit, Coarse, Fine, Q, OutLimit, KeyTrack, Tap, Strike,   // milestone 1
        FmAmt, Morph, MorphEnds, Width,                                           // milestone 2
        Level,                                                                    // milestone 1 (master strip)
        numSlots
    };
    const char* id (int filter, int slot);          // e.g. id (2, Coarse) = "gCoarse"

    // Master strip
    inline constexpr const char* masterLimit = "masterLimit";
    inline constexpr const char* linkAB      = "linkAB";       // M2
    inline constexpr const char* linkGD      = "linkGD";       // M2
    inline constexpr const char* offset      = "offset";       // M2
    inline constexpr const char* pitchSplit  = "pitchSplit";   // M2
    inline constexpr const char* lfoRate     = "lfoRate";      // M2
    extern const char* const busIds[numFilters];               // M2: "busA" "busB" "busG" "busD"
    inline constexpr const char* stereoMode  = "stereoMode";   // M3
    inline constexpr const char* pitchSnap   = "pitchSnap";    // M3
    inline constexpr const char* pitchScale  = "pitchScale";   // M3
    inline constexpr const char* pitchKey    = "pitchKey";     // M3
    inline constexpr const char* quality     = "quality";      // M3

    extern const float defaultCoarse[numFilters];              // 110, 220, 440, 1760 Hz

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // The texts shown on the panel and in Logic.
    juce::String hzText (float hz);                            // "A2 110 Hz", "A6 1.76 kHz"
    juce::String dbText (float db);                            // "0.0 dB", "-inf dB"
    float offsetSemitones (float knob);                        // the Offset knob's S-curve (M2)
}
