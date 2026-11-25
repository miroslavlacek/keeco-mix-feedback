#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace kmf
{
// FFT spectrum for display (runs on the UI thread, ~30x per second).
// 8192-point FFT, Hann window, 1/6-octave smoothing on 240 log-spaced points,
// optional 4.5 dB/octave tilt. Keeps three curves: live (fast), average since
// playback start, and peak hold.
class SpectrumAnalyzer
{
public:
    static constexpr int fftSize = 8192;
    static constexpr int numPoints = 240;
    static constexpr double minHz = 20.0, maxHz = 20000.0;

    enum class Mode { stereo, mid, side };

    SpectrumAnalyzer()
    {
        window.resize (fftSize);
        constexpr double pi = 3.14159265358979323846;
        double sum = 0.0;
        for (int i = 0; i < fftSize; ++i) { window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / (fftSize - 1)); sum += window[(size_t) i]; }
        windowGain = sum / fftSize;
        inL.assign (fftSize, 0.0f); inR.assign (fftSize, 0.0f);
        live.assign (numPoints, -120.0); average.assign (numPoints, -120.0); peak.assign (numPoints, -120.0);
        avgPower.assign (numPoints, 0.0);
        buf.resize (fftSize);
    }

    void setSampleRate (double newFs) { fs = newFs; reset(); }
    void setMode (Mode m) { mode = m; reset(); }
    void setTilt (bool on) { tilt = on; }
    void reset()
    {
        std::fill (live.begin(), live.end(), -120.0); std::fill (peak.begin(), peak.end(), -120.0);
        std::fill (average.begin(), average.end(), -120.0); std::fill (avgPower.begin(), avgPower.end(), 0.0);
        frames = 0;
    }

    // Feed the newest samples (any count). Keeps the last fftSize samples.
    void push (const float* l, const float* r, int n)
    {
        if (n >= fftSize) { l += n - fftSize; r += n - fftSize; n = fftSize; }
        std::move (inL.begin() + n, inL.end(), inL.begin());
        std::move (inR.begin() + n, inR.end(), inR.begin());
        std::copy (l, l + n, inL.end() - n);
        std::copy (r, r + n, inR.end() - n);
    }

    // Recompute curves. dt = seconds since the last call (for ballistics).
    void update (double dt, bool accumulateAverage)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            double x;
            switch (mode)
            {
                case Mode::side:   x = 0.5 * (inL[(size_t) i] - inR[(size_t) i]); break;
                case Mode::mid:
                case Mode::stereo:
                default:           x = 0.5 * (inL[(size_t) i] + inR[(size_t) i]); break;
            }
            buf[(size_t) i] = { x * window[(size_t) i], 0.0 };
        }
        fft (buf);

        // Power per bin, normalised so a full-scale sine reads about 0 dB.
        const int half = fftSize / 2;
        const double norm = 1.0 / (fftSize * 0.5 * windowGain);
        powerBins.resize ((size_t) half);
        for (int k = 0; k < half; ++k) powerBins[(size_t) k] = std::norm (buf[(size_t) k] * norm);

        const double binHz = fs / fftSize;
        const double liveCoeff = 1.0 - std::exp (-dt / 0.25);      // ~250 ms release
        const double peakFall = 12.0 * dt;                         // peak hold falls 12 dB/s
        if (accumulateAverage) ++frames;

        for (int p = 0; p < numPoints; ++p)
        {
            const double f = frequencyOf (p);
            const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);   // 1/6 octave
            int k0 = std::max (1, (int) std::floor (lo / binHz)), k1 = std::min (half - 1, (int) std::ceil (hi / binHz));
            double pw = 0.0; int cnt = 0;
            for (int k = k0; k <= k1; ++k) { pw += powerBins[(size_t) k]; ++cnt; }
            if (cnt == 0) { const int k = std::clamp ((int) std::lround (f / binHz), 1, half - 1); pw = powerBins[(size_t) k]; cnt = 1; }
            pw /= cnt;

            const double tiltDb = tilt ? 4.5 * std::log2 (f / 1000.0) : 0.0;
            const double db = toDb (pw) + tiltDb;

            live[(size_t) p] += liveCoeff * (db - live[(size_t) p]);
            peak[(size_t) p] = std::max (peak[(size_t) p] - peakFall, db);
            if (accumulateAverage)
            {
                avgPower[(size_t) p] += pw;
                average[(size_t) p] = toDb (avgPower[(size_t) p] / frames) + tiltDb;
            }
        }
    }

    static double frequencyOf (int point) { return minHz * std::pow (maxHz / minHz, (double) point / (numPoints - 1)); }

    const std::vector<double>& getLive() const    { return live; }
    const std::vector<double>& getAverage() const { return average; }
    const std::vector<double>& getPeak() const    { return peak; }

private:
    static double toDb (double p) { return p > 1e-14 ? 10.0 * std::log10 (p) : -140.0; }

    // Iterative radix-2 Cooley-Tukey FFT, in place.
    static void fft (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        constexpr double pi = 3.14159265358979323846;
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const std::complex<double> wl (std::cos (-2.0 * pi / (double) len), std::sin (-2.0 * pi / (double) len));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    double fs = 48000.0, windowGain = 0.5;
    Mode mode = Mode::stereo;
    bool tilt = true;
    long frames = 0;
    std::vector<double> window, live, average, peak, avgPower, powerBins;
    std::vector<float> inL, inR;
    std::vector<std::complex<double>> buf;
};
} // namespace kmf
