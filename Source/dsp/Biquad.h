#pragma once
#include <cmath>

namespace kmf
{
// Direct-form-II-transposed biquad. Coefficients are normalised (a0 == 1).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    static constexpr double pi = 3.14159265358979323846;

    // RBJ cookbook low-pass / high-pass (Q = 0.7071 gives Butterworth; two in a row = Linkwitz-Riley 4).
    static Biquad lowPass (double fs, double f0, double q = 0.70710678118654752)
    {
        const double w = 2.0 * pi * f0 / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 - c) / 2.0 / a0; b.b1 = (1.0 - c) / a0; b.b2 = b.b0;
        b.a1 = -2.0 * c / a0;        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    static Biquad highPass (double fs, double f0, double q = 0.70710678118654752)
    {
        const double w = 2.0 * pi * f0 / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 + c) / 2.0 / a0; b.b1 = -(1.0 + c) / a0; b.b2 = b.b0;
        b.a1 = -2.0 * c / a0;        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    // ITU-R BS.1770-4 K-weighting, stage 1 (high shelf), valid for any sample rate.
    // Constants as used by libebur128 / pyloudnorm.
    static Biquad kWeightShelf (double fs)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0;
        b.b1 = 2.0 * (K * K - Vh) / a0;
        b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        return b;
    }

    // ITU-R BS.1770-4 K-weighting, stage 2 (RLB high-pass).
    static Biquad kWeightHighPass (double fs)
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        return b;
    }
};
} // namespace kmf
