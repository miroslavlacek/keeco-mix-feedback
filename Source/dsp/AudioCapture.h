#pragma once
#include "Biquad.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace kmf
{
// Records the current playback as 16 kHz mono 16-bit audio, so a listening model (Gemini)
// can hear a chosen range of bars. Gemini works at 16 kHz mono internally anyway, so this
// keeps the files small without losing anything it would hear.
// push() runs on the audio thread and never allocates; writeWav() may run on any other thread.
class AudioCapture
{
public:
    static constexpr int outRate = 16000;
    static constexpr int maxBars = 4096;

    void prepare (double sampleRate, double maxSeconds = 600.0)
    {
        fs = sampleRate;
        ratio = fs / outRate;
        buffer.assign ((size_t) (maxSeconds * outRate), 0);
        lp1 = Biquad::lowPass (fs, 7000.0, 0.5412);    // 4th-order Butterworth anti-alias
        lp2 = Biquad::lowPass (fs, 7000.0, 1.3066);
        barStart = std::vector<std::atomic<int>> (maxBars);
        reset();
    }

    void reset() noexcept
    {
        lp1.reset(); lp2.reset();
        prev = 0.0; inputIndex = 0; nextOut = 0.0;
        written.store (0);
        for (auto& b : barStart) b.store (-1);
        lastBar = -1;
    }

    // bar = bar number at the first sample of this block (0 if the host gives no position).
    void push (const float* left, const float* right, int numSamples, int bar) noexcept
    {
        if (buffer.empty()) return;
        if (bar > 0 && bar < maxBars && bar != lastBar)
        {
            int expected = -1;
            barStart[(size_t) bar].compare_exchange_strong (expected, written.load());
            lastBar = bar;
        }

        int w = written.load (std::memory_order_relaxed);
        const int cap = (int) buffer.size();
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = lp2.process (lp1.process (0.5 * ((double) left[i] + (double) (right ? right[i] : left[i]))));
            while (nextOut <= (double) inputIndex && w < cap)
            {
                const double frac = nextOut - (double) (inputIndex - 1);       // position between prev and x
                const double y = prev + (x - prev) * frac;
                buffer[(size_t) w++] = (int16_t) std::lround (std::clamp (y, -1.0, 1.0) * 32767.0);
                nextOut += ratio;
            }
            prev = x;
            ++inputIndex;
        }
        written.store (w, std::memory_order_release);
    }

    int samplesWritten() const noexcept { return written.load (std::memory_order_acquire); }
    double secondsWritten() const noexcept { return samplesWritten() / (double) outRate; }

    // Sample range for bars [fromBar, toBar]. fromBar/toBar <= 0 mean "from start"/"to end".
    bool rangeForBars (int fromBar, int toBar, int& start, int& end) const noexcept
    {
        const int total = samplesWritten();
        start = 0; end = total;
        if (fromBar > 0)
        {
            if (fromBar >= maxBars || barStart[(size_t) fromBar].load() < 0) return false;
            start = barStart[(size_t) fromBar].load();
        }
        if (toBar > 0 && toBar + 1 < maxBars && barStart[(size_t) toBar + 1].load() >= 0)
            end = barStart[(size_t) toBar + 1].load();
        return end > start;
    }

    // First and last bar heard in this playback (0 if none).
    void barSpan (int& first, int& last) const noexcept
    {
        first = last = 0;
        for (int b = 1; b < maxBars; ++b)
            if (barStart[(size_t) b].load() >= 0) { if (first == 0) first = b; last = b; }
    }

    bool writeWav (const std::string& path, int start, int end) const
    {
        end = std::min (end, samplesWritten());
        if (end <= start) return false;
        std::FILE* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) return false;
        const uint32_t dataBytes = (uint32_t) (end - start) * 2u;
        auto u32 = [f] (uint32_t v) { unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
        auto u16 = [f] (uint16_t v) { unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (1); u32 (outRate); u32 (outRate * 2); u16 (2); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (dataBytes);
        for (int i = start; i < end; ++i) u16 ((uint16_t) buffer[(size_t) i]);   // little-endian
        const bool ok = std::ferror (f) == 0;
        std::fclose (f);
        return ok;
    }

private:
    double fs = 48000.0, ratio = 3.0;
    std::vector<int16_t> buffer;
    Biquad lp1, lp2;
    double prev = 0.0, nextOut = 0.0;
    long long inputIndex = 0;
    std::atomic<int> written { 0 };
    std::vector<std::atomic<int>> barStart;
    int lastBar = -1;
};
} // namespace kmf
