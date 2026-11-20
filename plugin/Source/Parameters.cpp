#include "Parameters.h"
#include <pedwar/Params.h>
#include <pedwar/Scales.h>
#include <array>

namespace PdParams
{

namespace
{
    const char* const letters[numFilters] = { "a", "b", "g", "d" };
    const char* const names[numFilters] = { "Alpha", "Beta", "Gamma", "Delta" };
    const char* const slotIds[numSlots] = {
        "Mode", "Gain", "InLimit", "Coarse", "Fine", "Q", "OutLimit", "KeyTrack", "Tap", "Strike",
        "FmAmt", "Morph", "MorphEnds", "Width", "Level" };

    std::array<std::array<juce::String, numSlots>, numFilters> buildIds()
    {
        std::array<std::array<juce::String, numSlots>, numFilters> r;
        for (int f = 0; f < numFilters; ++f)
            for (int s = 0; s < numSlots; ++s)
                r[(size_t) f][(size_t) s] = juce::String (letters[f]) + slotIds[s];
        return r;
    }
    const auto allIds = buildIds();

    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using TextFn = std::function<juce::String (float, int)>;

    void addFloat (Layout& l, const juce::String& pid, const juce::String& name, juce::NormalisableRange<float> range,
                   float def, TextFn text)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { pid, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    }
    void addChoice (Layout& l, const juce::String& pid, const juce::String& name, const juce::StringArray& items, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid, 1 }, name, items, def));
    }
    void addBool (Layout& l, const juce::String& pid, const juce::String& name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid, 1 }, name, def));
    }

    // A range that turns logarithmically (equal steps of the knob = equal musical steps).
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
    }
    juce::NormalisableRange<float> skewed (float lo, float hi, float skew)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.0f);
        r.skew = skew;
        return r;
    }

    juce::String percent (float v, int)  { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
    juce::String bipolar (float v, int)  { const int p = juce::roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + juce::String (p) + "%"; }
    juce::String centsText (float v, int) { const int c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " ct"; }
    juce::String qText (float v, int)    { return v < 9.95f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)); }
    juce::String morphText (float v, int)
    {
        // Left to right: LP end, x4, x2, x1 (the notch), x1/2, x1/4, HP (or BP) end. See pedwar::morphWeights.
        const float r = pedwar::morphMultiple (v);
        if (r < 0.0f)  return "HP / BP";
        if (r < 1.0e-6f) return "LP";
        const juce::String x = juce::String::fromUTF8 ("\xc3\x97");      // the multiplication sign
        const float oct = std::log2 (r);
        if (std::fabs (oct + 2.0f) < 0.03f) return x + juce::String::fromUTF8 ("\xc2\xbc");
        if (std::fabs (oct + 1.0f) < 0.03f) return x + juce::String::fromUTF8 ("\xc2\xbd");
        if (std::fabs (oct) < 0.03f)        return x + "1";
        if (std::fabs (oct - 1.0f) < 0.03f) return x + "2";
        if (std::fabs (oct - 2.0f) < 0.03f) return x + "4";
        return x + juce::String (r, r < 10.0f ? 2 : 1);
    }
    juce::String rateText (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }
}

const char* const busIds[numFilters] = { "busA", "busB", "busG", "busD" };
const float defaultCoarse[numFilters] = { 110.0f, 220.0f, 440.0f, 1760.0f };

const char* id (int filter, int slot)
{
    return allIds[(size_t) juce::jlimit (0, numFilters - 1, filter)][(size_t) juce::jlimit (0, numSlots - 1, slot)].toRawUTF8();
}

juce::String hzText (float hz)
{
    static const char* const noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int note = juce::roundToInt (69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f));
    const juce::String name = juce::String (noteNames[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    // (juce::String with 0 decimals would print every digit, so whole hertz are rounded first)
    if (hz < 100.0f) return name + " " + juce::String (hz, 1) + " Hz";
    if (hz < 1000.0f) return name + " " + juce::String (juce::roundToInt (hz)) + " Hz";
    return name + " " + juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
}

juce::String dbText (float db)
{
    if (db <= -59.95f) return "-inf dB";
    return juce::String (db, 1) + " dB";
}

float offsetSemitones (float knob)
{
    // S-curve: fine near the centre, big steps at the ends. Full turn = +-48 semitones (the engine's own curve).
    return pedwar::offsetSemitones (knob);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout l;

    for (int f = 0; f < numFilters; ++f)
    {
        auto pid = [f] (int slot) { return allIds[(size_t) f][(size_t) slot]; };
        auto n = [f] (const char* what) { return juce::String (names[f]) + " " + what; };

        addChoice (l, pid (Mode), n ("mode"), { "Filter", "Res", "Osc" }, 0);
        addFloat (l, pid (Gain), n ("gain"), skewed (-60.0f, 12.0f, 2.8f), 0.0f, [] (float v, int) { return dbText (v); });
        addBool (l, pid (InLimit), n ("in limit"), false);
        addFloat (l, pid (Coarse), n ("coarse"), logRange (16.0f, 20000.0f), defaultCoarse[f], [] (float v, int) { return hzText (v); });
        addFloat (l, pid (Fine), n ("fine"), juce::NormalisableRange<float> (-100.0f, 100.0f, 0.0f), 0.0f, centsText);
        addFloat (l, pid (Q), n ("Q"), logRange (0.5f, 50.0f), 2.0f, qText);
        addBool (l, pid (OutLimit), n ("out limit"), false);
        addBool (l, pid (KeyTrack), n ("key track"), true);
        addChoice (l, pid (Tap), n ("output tap"), { "Stereo morph", "LP", "BP", "HP", "Notch" }, 0);
        addBool (l, pid (Strike), n ("strike"), true);
        addFloat (l, pid (FmAmt), n ("FM amount"), juce::NormalisableRange<float> (-1.0f, 1.0f, 0.0f), 0.0f, bipolar);
        addFloat (l, pid (Morph), n ("morph"), juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.5f, morphText);
        addChoice (l, pid (MorphEnds), n ("morph ends"), { "LP-HP", "BP" }, 0);
        addFloat (l, pid (Width), n ("width"), juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.35f, percent);
        addFloat (l, pid (Level), n ("level"), skewed (-60.0f, 6.0f, 1.6f), -12.0f, [] (float v, int) { return dbText (v); });
    }

    addBool (l, masterLimit, "Master limit", false);
    addBool (l, linkAB, "Link alpha to beta", false);
    addBool (l, linkGD, "Link delta to gamma", false);
    addFloat (l, offset, "Offset", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.0f), 0.0f,
              [] (float v, int) { const float st = offsetSemitones (v);
                                  if (std::fabs (st) < 0.05f) return juce::String ("0 st");
                                  return (st > 0.0f ? "+" : "") + (std::fabs (st) < 9.95f ? juce::String (st, 1) : juce::String (juce::roundToInt (st))) + " st"; });
    addChoice (l, pitchSplit, "Pitch gamma-delta", { "All", "Split" }, 0);
    addFloat (l, lfoRate, "Rate", logRange (0.01f, 20.0f), 0.2f, rateText);
    for (int f = 0; f < numFilters; ++f)
        addChoice (l, busIds[f], juce::String ("LFO ") + names[f] + " bus", { "Width", "FM", "Anim" }, 0);
    addChoice (l, stereoMode, "Stereo", { "Sum", juce::String::fromUTF8 ("\xc3\x9c" "ber") }, 0);
    addChoice (l, pitchSnap, "Pitch Snap", { "Off", "Semitones", "Steps" }, 0);
    juce::StringArray scaleNames { "Off" };
    for (int i = 0; i < pedwar::scales::kNumScales; ++i)
        scaleNames.add (pedwar::scales::kScales[i].name);
    addChoice (l, pitchScale, "Pitch Scale", scaleNames, 0);
    addChoice (l, pitchKey, "Key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    addChoice (l, quality, "Quality", { "2x", "4x", "8x" }, 1);

    return l;
}

} // namespace PdParams
