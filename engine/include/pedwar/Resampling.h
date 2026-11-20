// PEDWAR engine: up to the oversampled rate and back down.
//
// Both use the same linear-phase low-pass filter (windowed sinc, Kaiser window), designed for the actual sample rate:
// everything up to 20 kHz is kept, and everything that would fold back into the audible range is removed by about
// 100 dB. The Decimator is the one from COMPLICATOR; the Upsampler is its mirror image (polyphase).

#pragma once
#include <vector>

namespace pedwar
{
    std::vector<float> designLowpass (double baseRate, int factor);   // the shared low-pass taps (sum = 1)

    class Upsampler
    {
    public:
        void prepare (double baseRate, int factor);
        void reset();
        // One project-rate sample in, 'factor' oversampled samples out.
        void process (float x, float* out);
        double latency() const { return ((double) numTaps - 1.0) * 0.5 / (double) fac; }   // in project-rate samples

    private:
        int fac = 4, numTaps = 0, perPhase = 0;
        std::vector<float> phases;   // fac rows of perPhase taps
        std::vector<float> hist;     // twice perPhase, so the newest samples are one straight run
        int pos = 0;
        int zeroRun = 0;             // how many silent samples in a row went in
    };

    class Decimator
    {
    public:
        void prepare (double baseRate, int factor);
        void reset();
        // Feed exactly 'factor' oversampled samples; returns one output sample.
        float process (const float* in);
        int factor() const { return fac; }
        double latency() const { return ((double) taps.size() - 1.0) * 0.5 / (double) fac; }

    private:
        int fac = 4;
        std::vector<float> taps;
        std::vector<float> buf;
        int pos = 0;
    };
} // namespace pedwar
