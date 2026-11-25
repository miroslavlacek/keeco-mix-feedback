#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace kmf
{
// True-peak meter per ITU-R BS.1770-4 Annex 2: 4x oversampling with a polyphase
// windowed-sinc interpolator (12 taps per phase), then absolute maximum.
// At 96 kHz and above 2x would be enough; 4x everywhere keeps it simple.
class TruePeakMeter
{
public:
    static constexpr int factor = 4, tapsPerPhase = 12, numTaps = factor * tapsPerPhase;

    TruePeakMeter()
    {
        // Windowed sinc, cutoff at the original Nyquist, Kaiser-like Blackman window.
        constexpr double pi = 3.14159265358979323846;
        const double centre = (numTaps - 1) / 2.0;
        for (int n = 0; n < numTaps; ++n)
        {
            const double x = (n - centre) / factor;
            const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double w = 0.42 - 0.5 * std::cos (2.0 * pi * n / (numTaps - 1))
                                  + 0.08 * std::cos (4.0 * pi * n / (numTaps - 1));
            coeffs[(size_t) n] = sinc * w;
        }
        // Normalise each phase to unity DC gain so a constant signal reads correctly.
        for (int p = 0; p < factor; ++p)
        {
            double s = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k) s += coeffs[(size_t) (p + k * factor)];
            for (int k = 0; k < tapsPerPhase; ++k) coeffs[(size_t) (p + k * factor)] /= s;
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& h : history) h.fill (0.0f);
        pos = 0;
    }

    // Returns the linear true-peak of this stereo sample frame (max over L, R and the 4 phases).
    inline float processSample (float left, float right) noexcept
    {
        history[0][(size_t) pos] = left;
        history[1][(size_t) pos] = right;

        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int p = 0; p < factor; ++p)
            {
                double acc = 0.0;
                for (int k = 0; k < tapsPerPhase; ++k)
                    acc += coeffs[(size_t) (p + k * factor)]
                         * history[(size_t) ch][(size_t) ((pos - k + tapsPerPhase) % tapsPerPhase)];
                peak = std::max (peak, (float) std::abs (acc));
            }
        }
        pos = (pos + 1) % tapsPerPhase;
        return peak;
    }

    static double toDb (double linear) noexcept { return linear > 1e-10 ? 20.0 * std::log10 (linear) : -200.0; }

private:
    std::array<double, numTaps> coeffs {};
    std::array<std::array<float, tapsPerPhase>, 2> history {};
    int pos = 0;
};
} // namespace kmf
