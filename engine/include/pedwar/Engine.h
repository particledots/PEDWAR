// PEDWAR engine: four state-variable filters / resonators, their stereo morph and width, four LFOs, the limiters and
// the master mix. Plain C++17, no plugin code.
//
// The plugin wrapper hands it the settings (Params), the MIDI notes and the audio; the engine hands stereo audio back.
// Everything inside runs at 2x, 4x (the default) or 8x the project's sample rate (the Quality menu) and is brought back
// down with a steep filter, so the self-oscillation, the shapers and the limiters stay clean. Changing Quality fades
// the output out (4 ms), switches, waits for the new down-sampling filter to fill and fades back in (4 ms). The latency
// is the same at every Quality (a small extra delay at the project rate makes up the difference), so Logic's latency
// compensation never has to change.
//
// Signal flow per oversampled sample:
//   inputs:  alpha and beta take the track audio. gamma and delta take the side chain while it carries audio (it is
//            counted as present until it has been silent for 2 seconds), otherwise the track audio too; the change is
//            cross-faded. If the track input has been silent for 2 seconds while the side chain plays (PEDWAR in the
//            instrument slot of a Software Instrument track), alpha and beta take the side chain too.
//            In PEDWAR Synth (no track audio) the side chain feeds all four.
//   each filter:  Gain -> In limit (tanh) -> state-variable core (Filter / Res / Osc)
//                 -> Output tap: stereo morph (Morph + Width, below) or LP / BP / HP / Notch on both sides
//                 -> Out limit -> Level -> master stereo mix
//   master:  Stereo sum / uber -> Master limit -> output.
//            sum:  left = every filter's left side, right = every filter's right side.
//            uber: left = alpha's and beta's right sides (their all-pass blend), right = gamma's and delta's left sides
//                  (theirs), both times 1.58 (+4 dB) so uber is about as loud as sum (it drops 3 to 6 dB without: half
//                  the sources on each side). The switch cross-fades over 20 ms.
//
// Stereo morph: Morph mixes the core's LP, HP and BP into a moved notch (see morphWeights in Params.h). Width: one side
// (right on alpha / beta, left on gamma / delta) is an equal-power blend of that signal and a copy through two
// first-order all-pass stages whose corner sits at the filter's frequency, swept +-2 octaves by the filter's own LFO
// while that LFO's bus is "width".
//
// LFOs (project rate): four triangles at Rate x 1, 1.127, 0.871, 1.293 (alpha .. delta), so they drift against each
// other. The bus grid sends each one to:
//   width  LFO n sweeps filter n's all-pass.
//   FM     LFO n moves the previous filter's cutoff (alpha -> delta, beta -> alpha, gamma -> beta, delta -> gamma),
//          times that filter's FM amt (full = +-3 octaves).
//   anim   filter n's FM comes from LFO n+1 (times its FM amt), its Q from LFO n+2 (+-1.5 octaves of Q) and its Morph
//          from LFO n+3 (+-0.25 of the knob); LFO n+1 also nudges LFO n's speed (+-0.12 octave).
//   (Filter n's FM source is LFO n+1 both when LFO n+1 is on "FM" and when column n is on "anim"; it is the same
//   connection, so it is not counted twice.) Every bus change is cross-faded over 20 ms.
// Link: the follower (beta, gamma) adds the leader's (alpha, delta) Coarse, Fine, FM amt, Q, Width and Morph, measured
// from the leader's defaults, and the leader's LFO movement of cutoff, Q and Morph.
// Offset moves all four cutoffs (S-curve, +-48 semitones). Pitch gamma-delta "split": notes below middle C key-track
// gamma and delta, notes from middle C up key-track alpha and beta.
//
// The core is a trapezoidal ("zero-delay feedback") state-variable filter, which stays stable however fast its
// frequency and resonance move. Its damping k = 1/Q sets the resonance:
//   Filter  k = 1/Q, a normal linear filter (Q 0.5 .. 50).
//   Res     k = 1/Q - 0.06 + 0.16 E: from Q of about 17 upwards k would go below zero and the filter starts to
//           oscillate; the term in E (the filter's own energy) stops it growing, so it settles into a sine whose
//           level grows the further Q is turned past that point (about 0.5 at Q 50).
//   Osc     k = -a (1 - E / 0.25): the level is held at 0.5 (LP output) at every frequency. Q sets how firmly
//           (low Q holds it tight, high Q lets strikes and input swell it more).
// Strike adds a kick to the band-pass state, so the filter rings at its own frequency, as loud as the velocity says.
//
// Pitch Snap, Scale and Key (Pitch.h) snap each filter's own tuning (Coarse + Fine) before key tracking, Link, Offset
// and the LFOs are added. With Snap Off and Scale Off nothing is snapped and the tuning is exactly as before.

#pragma once
#include "Nonlinear.h"
#include "Params.h"
#include "Pitch.h"
#include "Resampling.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace pedwar
{
    // dB -> gain; the bottom of each range (-60 dB) is silence.
    inline float dbToGain (float db) { return db <= -59.95f ? 0.0f : std::pow (10.0f, db * 0.05f); }

    class Engine
    {
    public:
        static constexpr int kMaxFactor = 8;         // Quality: 2x, 4x (default), 8x
        static constexpr int kNumQualities = 3;
        static constexpr int factorFor (int quality) { return quality <= 0 ? 2 : (quality >= 2 ? 8 : 4); }
        static constexpr double kQualityFade = 0.004; // seconds: fade out, and back in, when Quality changes
        static constexpr float kUberGain = 1.5849f;     // +4 dB: uber about as loud as sum
        static constexpr float kStrikeKick = 0.7f;   // ringing level of a full-velocity strike
        static constexpr float kOscLevel = 0.5f;     // Osc mode sine level (LP output)
        static constexpr double kSidechainHold = 2.0; // seconds of silence before gamma/delta go back to the track

        // LFOs and modulation depths
        static constexpr float kLfoRatio[kNumFilters] = { 1.0f, 1.127f, 0.871f, 1.293f };
        static constexpr float kFmOctaves = 3.0f;      // FM amt at full: +-3 octaves
        static constexpr float kAnimQOctaves = 1.5f;   // anim: Q moved +-1.5 octaves (x0.35 .. x2.8)
        static constexpr float kAnimMorph = 0.25f;     // anim: Morph moved +-0.25 of the knob (+-1 octave of the notch)
        static constexpr float kAnimRateOctaves = 0.12f; // anim: the next LFO nudges this LFO's speed (+-9%)
        static constexpr float kApSweepOctaves = 2.0f; // width bus: the all-pass corner swept +-2 octaves

        Engine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // PEDWAR Synth: the side chain feeds all four filters (there is no track audio).
        void setSidechainFeedsAll (bool on) { sideFeedsAll = on; }

        // Audio thread only (the wrapper calls these from its processBlock, in time order).
        void setParams (const Params& p) { params = p; }
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();

        // track / side: mono, numSamples each, either may be nullptr (= silence). outL / outR: numSamples each.
        void process (const float* track, const float* side, float* outL, float* outR, int numSamples);

        // The same at every Quality (whole project-rate samples).
        double latencySamples() const { return (double) latencyAll; }
        // The Quality now running (2, 4 or 8). While a change fades, the old one until the switch.
        int oversamplingFactor() const { return factorFor (qActive); }
        bool qualitySwitching() const { return qPhase != 0; }

        // For the panel (any thread).
        int strikeCount (int f) const { return strikes[f].load (std::memory_order_relaxed); }
        bool sidechainPresent() const { return scShown.load (std::memory_order_relaxed); }
        bool filterUsesSidechain (int f) const { return usingSide[f].load (std::memory_order_relaxed); }
        float playedHz (int f) const { return shownHz[f].load (std::memory_order_relaxed); }
        float lfoValue (int j) const { return lfoShown[j].load (std::memory_order_relaxed); }       // -1..+1

        // Where the LFOs are moving a filter's Coarse, Q and Morph right now ('now') and how far they can swing it
        // ('depth', always >= 0), in octaves (Coarse, Q) and knob units (Morph). Includes what Link passes on.
        enum ModTarget { ModCoarse = 0, ModQ = 1, ModMorph = 2, kNumModTargets = 3 };
        float modNow (int f, int target) const   { return modNowShown[f][target].load (std::memory_order_relaxed); }
        float modDepth (int f, int target) const { return modDepthShown[f][target].load (std::memory_order_relaxed); }

        // The frequency a filter's own knobs ask for (before Link, Offset, LFOs and the safety limits), for 'note'
        // (60 = no key-track shift), with Pitch Snap, Scale and Key (Pitch.h).
        static float targetHz (const FilterParams& fp, int note, int snap = 0, int scale = 0, int key = 0);
        // Only the knobs' tuning (Coarse + Fine, snapped), for the panel's Coarse readout: as targetHz for note 60.
        static float knobHz (const FilterParams& fp, int snap, int scale, int key) { return targetHz (fp, 60, snap, scale, key); }

    private:
        struct Ramp      // linear move towards 0 or 1, used for every switch so none of them clicks
        {
            float v = 0.0f;
            void to (float target, float step) { v = v < target ? std::min (target, v + step) : std::max (target, v - step); }
        };

        struct Filter
        {
            double ic1 = 0.0, ic2 = 0.0;      // the two integrator states (band-pass, low-pass)
            double energy = 0.0;              // bp^2 + lp^2 of the previous oversampled sample
            double kick = 0.0;                // strike waiting to be added
            double ap1 = 0.0, ap2 = 0.0;      // the width all-pass states
            float logHz = 0.0f, logQ = 0.0f;  // smoothed, log2 (knobs, key track, Offset, Link; no LFO)
            float gain = 1.0f, level = 0.0f;  // smoothed, linear
            float morph = 0.5f, width = 0.35f, fm = 0.0f;   // smoothed knobs (with Link)
            float tLogHz = 0.0f, tLogQ = 0.0f, tGain = 1.0f, tLevel = 0.0f;   // where they are heading
            float tMorph = 0.5f, tWidth = 0.35f, tFm = 0.0f;
            float cLogHz = -1.0f, cLogQ = -100.0f, cLogAp = -1.0f;            // what g / kq / apG were worked out for
            double g = 0.0, kq = 0.5, alpha = 0.1, apG = 0.0;
            float cMorph = -1.0f, cBpEnds = -1.0f, cWidth = -1.0f;            // what the weights below were worked out for
            MorphWeights mw { 1.0f, 1.0f, 0.0f };
            double wDry = 1.0, wAp = 0.0;
            Ramp inLim, outLim, side, tap[kNumTaps], bpEnds;
            Ramp srcFm, srcQ, srcMorph, srcAp;   // the bus grid's connections into this filter
            Ramp link;                           // this filter follows its leader (beta, gamma only)
            nl::Adaa1<nl::Tanh> tanhIn;
            nl::Adaa1<nl::Knee> limL, limR;
        };

        // This project-rate sample's modulation of one filter.
        struct Mod { float pitchOct = 0.0f, qOct = 0.0f, morph = 0.0f, ap = 0.0f; float dPitch = 0.0f, dQ = 0.0f, dMorph = 0.0f; };

        void renderFilter (int f, const Mod& m, const float* tU, const float* sU, float* oL, float* oR, float* uL, float* uR,
                           bool first);
        void setQuality (int q);
        void updateTargets();
        void stepLfos();
        void computeMods (Mod* mods);
        int noteFor (int f) const;

        Params params;
        bool sideFeedsAll = false;
        double sr = 48000.0, osRate = 192000.0;
        int fac = 4;
        float smoothPitch = 0.1f, smoothQ = 0.05f, smoothGain = 0.05f, smoothKnob = 0.05f, smoothRate = 0.01f;
        float rampStep = 0.01f, sideStep = 0.001f, modStep = 0.002f;
        float maxHz = 20000.0f;

        Filter flt[kNumFilters];
        Upsampler up[kNumQualities][2];   // per Quality: track, side (the idle ones are kept filled, see process)
        Decimator dec[kNumQualities][2];  // per Quality: left, right
        int qActive = 1;                  // the Quality running now (0 = 2x, 1 = 4x, 2 = 8x)
        int qPhase = 0;                   // 0 = steady, 1 = fading out, 2 = waiting for the new filter, 3 = fading in
        float qGain = 1.0f, qStep = 0.01f;
        int qWait = 0;
        int latencyAll = 0;               // whole samples, the same at every Quality
        int extraDelay[kNumQualities] = {};
        static constexpr int kDelayLen = 16;
        float delL[kDelayLen] = {}, delR[kDelayLen] = {};
        int delPos = 0;
        Ramp masterLim, uber;
        nl::Adaa1<nl::Knee> masterL, masterR;
        int scHold = 0, trackHold = 0, scHoldSamples = 96000;

        double lfoPhase[kNumFilters] = {};
        float lfo[kNumFilters] = {};
        float logRate = 0.0f;     // smoothed log2 of Rate

        int noteStack[128] = {};
        int stackSize = 0;
        int noteAll = 60, noteHigh = 60, noteLow = 60;   // last note (any), last from middle C up, last below it

        std::atomic<int> strikes[kNumFilters];
        std::atomic<bool> scShown { false };
        std::atomic<bool> usingSide[kNumFilters];
        std::atomic<float> shownHz[kNumFilters];
        std::atomic<float> lfoShown[kNumFilters];
        std::atomic<float> modNowShown[kNumFilters][kNumModTargets];
        std::atomic<float> modDepthShown[kNumFilters][kNumModTargets];
    };
} // namespace pedwar
