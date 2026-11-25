#pragma once
#include "Biquad.h"
#include <array>
#include <cmath>

namespace kmf
{
// Splits the mid signal into 5 bands with Linkwitz-Riley 4 crossovers and measures
// band energy and low-band stereo correlation. Used for rules, summaries and learning
// a reference; the FFT spectrum in the UI is only for display.
class BandMeter
{
public:
    static constexpr int numBands = 5;
    static constexpr std::array<double, numBands - 1> crossovers { 60.0, 250.0, 500.0, 4000.0 };
    static constexpr const char* bandNames[numBands] { "Sub 20–60 Hz", "Bas 60–250 Hz", "Nízke stredy 250–500 Hz",
                                                       "Stredy 0,5–4 kHz", "Výšky 4 kHz+" };
    static constexpr double monoBassFreq = 120.0;

    void prepare (double fs)
    {
        for (int i = 0; i < numBands - 1; ++i)
            for (int s = 0; s < 2; ++s)
            {
                lp[i][s] = Biquad::lowPass  (fs, crossovers[(size_t) i]);
                hp[i][s] = Biquad::highPass (fs, crossovers[(size_t) i]);
            }
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 2; ++s)
                bassLp[ch][s] = Biquad::lowPass (fs, monoBassFreq);
        reset();
    }

    void reset()
    {
        for (auto& row : lp) for (auto& f : row) f.reset();
        for (auto& row : hp) for (auto& f : row) f.reset();
        for (auto& row : bassLp) for (auto& f : row) f.reset();
        clearAccumulators();
    }

    inline void processSample (float left, float right) noexcept
    {
        // Band split of the mid signal: peel off each band with a low-pass, continue with the high-pass.
        double rest = 0.5 * ((double) left + (double) right);
        for (int i = 0; i < numBands - 1; ++i)
        {
            const double low  = lp[i][1].process (lp[i][0].process (rest));
            rest              = hp[i][1].process (hp[i][0].process (rest));
            energy[(size_t) i] += low * low;
        }
        energy[numBands - 1] += rest * rest;

        // Low-band stereo check below 120 Hz.
        const double l = bassLp[0][1].process (bassLp[0][0].process (left));
        const double r = bassLp[1][1].process (bassLp[1][0].process (right));
        bassLL += l * l; bassRR += r * r; bassLR += l * r;
        // Whole-signal correlation and side share.
        allLL += (double) left * left; allRR += (double) right * right; allLR += (double) left * right;
        ++count;
    }

    struct Snapshot
    {
        std::array<double, numBands> bandDb {};   // band level, dB (mean square of mid band)
        double bassCorrelation = 1.0;            // -1..+1, below 120 Hz
        double bassLevelDb = -200.0;
        double correlation = 1.0;                // -1..+1, full band
        double width = 0.0;                      // side energy share 0..1
    };

    // Call once per 100 ms step (same moment the loudness meter finishes a step).
    Snapshot takeSnapshot() noexcept
    {
        Snapshot s;
        const double n = count > 0 ? (double) count : 1.0;
        for (int i = 0; i < numBands; ++i)
            s.bandDb[(size_t) i] = toDb (energy[(size_t) i] / n);
        s.bassCorrelation = correlationOf (bassLL, bassRR, bassLR);
        s.bassLevelDb     = toDb ((bassLL + bassRR) * 0.5 / n);
        s.correlation     = correlationOf (allLL, allRR, allLR);
        const double mid  = (allLL + allRR + 2.0 * allLR) * 0.25;
        const double side = (allLL + allRR - 2.0 * allLR) * 0.25;
        s.width = (mid + side) > 1e-20 ? side / (mid + side) : 0.0;
        clearAccumulators();
        return s;
    }

    static double toDb (double meanSquare) noexcept { return meanSquare > 1e-20 ? 10.0 * std::log10 (meanSquare) : -200.0; }

private:
    static double correlationOf (double ll, double rr, double lr) noexcept
    {
        const double d = std::sqrt (ll * rr);
        return d > 1e-20 ? lr / d : 1.0;   // silence counts as mono
    }

    void clearAccumulators() noexcept
    {
        energy.fill (0.0);
        bassLL = bassRR = bassLR = allLL = allRR = allLR = 0.0;
        count = 0;
    }

    Biquad lp[numBands - 1][2], hp[numBands - 1][2], bassLp[2][2];
    std::array<double, numBands> energy {};
    double bassLL = 0, bassRR = 0, bassLR = 0, allLL = 0, allRR = 0, allLR = 0;
    long count = 0;
};
} // namespace kmf
