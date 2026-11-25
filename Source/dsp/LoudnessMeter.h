#pragma once
#include "Biquad.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace kmf
{
// EBU R128 / ITU-R BS.1770-4 loudness meter for a stereo signal.
// Real-time safe: no allocation after prepare(). Call process() from the audio thread;
// every completed 100 ms step is reported through stepFinished().
class LoudnessMeter
{
public:
    static constexpr double silence = -200.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            shelf[ch] = Biquad::kWeightShelf (fs);
            hp[ch]    = Biquad::kWeightHighPass (fs);
        }
        stepLength = std::max (1, (int) std::lround (fs * 0.1));
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { shelf[ch].reset(); hp[ch].reset(); }
        stepAccum = 0.0; stepCount = 0;
        ring.fill (0.0); ringPos = 0; stepsSeen = 0;
        intHist.fill ({}); lraHist.fill ({});
        momentary = shortTerm = integrated = silence;
        lra = 0.0;
    }

    // Returns true when a 100 ms step finished inside this sample.
    inline bool processSample (float left, float right) noexcept
    {
        const double l = hp[0].process (shelf[0].process (left));
        const double r = hp[1].process (shelf[1].process (right));
        stepAccum += l * l + r * r;               // channel weights L = R = 1.0
        if (++stepCount < stepLength)
            return false;

        finishStep (stepAccum / (double) stepCount);
        stepAccum = 0.0; stepCount = 0;
        return true;
    }

    double getMomentary()  const noexcept { return momentary; }   // 400 ms, LUFS
    double getShortTerm()  const noexcept { return shortTerm; }   // 3 s, LUFS
    double getIntegrated() const noexcept { return integrated; }  // gated, since reset, LUFS
    double getRange()      const noexcept { return lra; }         // LRA, LU

    static double toLufs (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-20 ? -0.691 + 10.0 * std::log10 (meanSquare) : silence;
    }

private:
    // Histogram from -70 LUFS to +10 LUFS in 0.1 dB bins, like libebur128.
    struct Bin { double energy = 0.0; long count = 0; };
    static constexpr int numBins = 800;
    static constexpr double histMin = -70.0, histStep = 0.1;

    void finishStep (double stepMeanSquare) noexcept
    {
        ring[(size_t) ringPos] = stepMeanSquare;
        ringPos = (ringPos + 1) % (int) ring.size();
        ++stepsSeen;

        momentary = toLufs (averageOfLast (4));
        shortTerm = toLufs (averageOfLast (30));

        // Gating blocks: 400 ms windows every 100 ms (75 % overlap).
        if (stepsSeen >= 4)
            addToHistogram (intHist, averageOfLast (4));

        // LRA uses short-term values (3 s windows), sampled every 100 ms.
        if (stepsSeen >= 30)
            addToHistogram (lraHist, averageOfLast (30));

        integrated = computeIntegrated();
        lra        = computeRange();
    }

    double averageOfLast (int n) const noexcept
    {
        n = (int) std::min<long> (n, stepsSeen);
        if (n <= 0) return 0.0;
        double sum = 0.0;
        for (int i = 1; i <= n; ++i)
            sum += ring[(size_t) ((ringPos - i + (int) ring.size()) % (int) ring.size())];
        return sum / n;
    }

    static void addToHistogram (std::array<Bin, numBins>& hist, double meanSquare) noexcept
    {
        const double lufs = toLufs (meanSquare);
        if (lufs < histMin) return;                                   // absolute gate -70 LUFS
        const int idx = std::clamp ((int) ((lufs - histMin) / histStep), 0, numBins - 1);
        hist[(size_t) idx].energy += meanSquare;
        hist[(size_t) idx].count  += 1;
    }

    static double binLufs (int idx) noexcept { return histMin + (idx + 0.5) * histStep; }

    double computeIntegrated() const noexcept
    {
        double e = 0.0; long n = 0;
        for (const auto& b : intHist) { e += b.energy; n += b.count; }
        if (n == 0) return silence;
        const double relGate = toLufs (e / n) - 10.0;

        e = 0.0; n = 0;
        for (int i = 0; i < numBins; ++i)
            if (binLufs (i) >= relGate) { e += intHist[(size_t) i].energy; n += intHist[(size_t) i].count; }
        return n > 0 ? toLufs (e / n) : silence;
    }

    double computeRange() const noexcept
    {
        double e = 0.0; long n = 0;
        for (const auto& b : lraHist) { e += b.energy; n += b.count; }
        if (n == 0) return 0.0;
        const double relGate = toLufs (e / n) - 20.0;

        long total = 0;
        for (int i = 0; i < numBins; ++i)
            if (binLufs (i) >= relGate) total += lraHist[(size_t) i].count;
        if (total == 0) return 0.0;

        const long lowTarget = (long) std::ceil (total * 0.10), highTarget = (long) std::ceil (total * 0.95);
        long seen = 0; double lo = 0.0, hi = 0.0; bool loSet = false;
        for (int i = 0; i < numBins; ++i)
        {
            if (binLufs (i) < relGate) continue;
            seen += lraHist[(size_t) i].count;
            if (! loSet && seen >= lowTarget) { lo = binLufs (i); loSet = true; }
            if (seen >= highTarget)          { hi = binLufs (i); break; }
        }
        return std::max (0.0, hi - lo);
    }

    double fs = 48000.0;
    Biquad shelf[2], hp[2];
    int stepLength = 4800, stepCount = 0;
    double stepAccum = 0.0;

    std::array<double, 30> ring {};
    int ringPos = 0;
    long stepsSeen = 0;

    std::array<Bin, numBins> intHist {}, lraHist {};
    double momentary = silence, shortTerm = silence, integrated = silence, lra = 0.0;
};
} // namespace kmf
