// PEDWAR engine: the shapers and limiters, band-limited.
//
// Each stage is a function f(x) with a known antiderivative F(x). First-order antiderivative anti-aliasing (ADAA)
// replaces f(x[n]) by the average of f over the straight line between the previous and the current input sample:
//
//        y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//
// On top of the 4x oversampling this rounds the corners, so far less of their harmonics fold back into the audible
// range. It adds half an oversampled sample of delay. The maths is done in double precision. Header only.

#pragma once
#include <cmath>

namespace pedwar
{
namespace nl
{
    // tanh: the input limiter ("In limit"). Small signals pass at unity gain, big ones are rounded towards +-1.
    struct Tanh
    {
        double f (double x) const { return std::tanh (x); }
        double F (double x) const          // log (cosh x), written so it never overflows
        {
            const double a = std::fabs (x);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
        }
    };

    // A soft knee: exactly linear up to 'knee', then a smooth bend up to a flat ceiling of knee + bend, reached at
    // knee + 2 * bend. The slope never jumps, so there is no hard corner anywhere.
    //   Out limit (each filter): knee 0.35, bend 0.35  -> ceiling 0.7  (aggressive: starts early, flattens fast)
    //   Master limit:            knee 0.6,  bend 0.4   -> ceiling 1.0  (very linear at low level)
    struct Knee
    {
        double knee = 0.6, bend = 0.4;

        double f (double x) const
        {
            const double a = std::fabs (x);
            double y;
            if (a <= knee) y = a;
            else if (a < knee + 2.0 * bend) { const double u = a - knee; y = knee + u - u * u / (4.0 * bend); }
            else y = knee + bend;
            return x < 0.0 ? -y : y;
        }
        double F (double x) const
        {
            const double a = std::fabs (x), t = knee, c = bend;
            if (a <= t) return 0.5 * a * a;
            if (a < t + 2.0 * c) { const double u = a - t; return 0.5 * t * t + t * u + 0.5 * u * u - u * u * u / (12.0 * c); }
            const double top = 0.5 * t * t + 2.0 * t * c + (4.0 / 3.0) * c * c;
            return top + (t + c) * (a - t - 2.0 * c);
        }
    };

    // A stage that is switched off is "unprimed": the first sample after switching it on starts its memory afresh.
    template <class Fn>
    struct Adaa1
    {
        Fn fn;
        double xp = 0.0, Fp = 0.0;
        bool primed = false;

        void unprime() { primed = false; }

        double next (double x)
        {
            const double Fx = fn.F (x);
            if (! primed) { xp = x; Fp = Fx; primed = true; return fn.f (x); }
            const double dx = x - xp;
            const double y = (std::fabs (dx) < 1.0e-7) ? fn.f (0.5 * (x + xp)) : (Fx - Fp) / dx;
            xp = x;
            Fp = Fx;
            return y;
        }
    };
} // namespace nl
} // namespace pedwar
