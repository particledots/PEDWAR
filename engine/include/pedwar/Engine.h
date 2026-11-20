// PEDWAR engine: four state-variable filters / resonators, their limiters, the master mix. Plain C++17, no plugin code.
//
// The plugin wrapper hands it the settings (Params), the MIDI notes and the audio; the engine hands stereo audio back.
// Everything inside runs at 4x the project's sample rate (the Quality menu, milestone 3, will offer 2x / 4x / 8x) and
// is brought back down with a steep filter, so the self-oscillation, the shapers and the limiters stay clean.
//
// Signal flow per oversampled sample (milestone 1):
//   inputs:  alpha and beta take the track audio. gamma and delta take the side chain while it carries audio (it is
//            counted as present until it has been silent for 2 seconds), otherwise the track audio too; the change is
//            cross-faded. If the track input has been silent for 2 seconds while the side chain plays (PEDWAR in the
//            instrument slot of a Software Instrument track), alpha and beta take the side chain too.
//            In PEDWAR Synth (no track audio) the side chain feeds all four.
//   each filter:  Gain -> In limit (tanh) -> state-variable core (Filter / Res / Osc) -> Output tap -> Out limit
//                 -> Level -> master stereo mix
//   master:  Master limit -> output.
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

#pragma once
#include "Nonlinear.h"
#include "Params.h"
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
        static constexpr int kFactor = 4;            // fixed until milestone 3
        static constexpr float kStrikeKick = 0.7f;   // ringing level of a full-velocity strike
        static constexpr float kOscLevel = 0.5f;     // Osc mode sine level (LP output)
        static constexpr double kSidechainHold = 2.0; // seconds of silence before gamma/delta go back to the track

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

        double latencySamples() const { return up[0].latency() + dec[0].latency(); }
        int oversamplingFactor() const { return kFactor; }

        // For the panel (any thread).
        int strikeCount (int f) const { return strikes[f].load (std::memory_order_relaxed); }
        bool sidechainPresent() const { return scShown.load (std::memory_order_relaxed); }
        bool filterUsesSidechain (int f) const { return usingSide[f].load (std::memory_order_relaxed); }
        float playedHz (int f) const { return shownHz[f].load (std::memory_order_relaxed); }

        // The frequency a filter's knobs ask for (before the safety limits), for 'note' (60 = no key-track shift).
        static float targetHz (const FilterParams& fp, int note);

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
            float logHz = 0.0f, logQ = 0.0f;  // smoothed, log2
            float gain = 1.0f, level = 0.0f;  // smoothed, linear
            float tLogHz = 0.0f, tLogQ = 0.0f, tGain = 1.0f, tLevel = 0.0f;   // where they are heading
            float cLogHz = -1.0f, cLogQ = -100.0f;                            // what g / kq were worked out for
            double g = 0.0, kq = 0.5, alpha = 0.1;
            Ramp inLim, outLim, side, tap[kNumTaps];
            nl::Adaa1<nl::Tanh> tanhIn;
            nl::Adaa1<nl::Knee> limL, limR;
        };

        void renderFilter (int f, const float* tU, const float* sU, float* oL, float* oR, bool first);
        void updateTargets();

        Params params;
        bool sideFeedsAll = false;
        double sr = 48000.0, osRate = 192000.0;
        float smoothPitch = 0.1f, smoothQ = 0.05f, smoothGain = 0.05f, rampStep = 0.01f, sideStep = 0.001f;
        float maxHz = 20000.0f;

        Filter flt[kNumFilters];
        Upsampler up[2];          // track, side
        Decimator dec[2];         // left, right
        Ramp masterLim;
        nl::Adaa1<nl::Knee> masterL, masterR;
        int scHold = 0, trackHold = 0, scHoldSamples = 96000;

        int noteStack[128] = {};
        int stackSize = 0;
        int currentNote = 60;

        std::atomic<int> strikes[kNumFilters];
        std::atomic<bool> scShown { false };
        std::atomic<bool> usingSide[kNumFilters];
        std::atomic<float> shownHz[kNumFilters];
    };
} // namespace pedwar
