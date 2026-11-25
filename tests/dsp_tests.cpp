// Standalone tests for the DSP core (no JUCE): g++ -std=c++17 -O2 -I../Source tests/dsp_tests.cpp
#include "dsp/MixAnalyzer.h"
#include "dsp/SpectrumAnalyzer.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdlib>

using namespace kmf;
static int failures = 0;
static void check (bool ok, const char* what, double got, double want)
{
    std::printf ("%s %-55s got %8.3f  want %8.3f\n", ok ? "PASS" : "FAIL", what, got, want);
    if (! ok) ++failures;
}

static constexpr double pi = 3.14159265358979323846;

static double integratedOfSine (double fs, double freq, double dbfs, double seconds)
{
    LoudnessMeter m; m.prepare (fs);
    const double a = std::pow (10.0, dbfs / 20.0);
    const long n = (long) (fs * seconds);
    for (long i = 0; i < n; ++i) { const float s = (float) (a * std::sin (2 * pi * freq * i / fs)); m.processSample (s, s); }
    return m.getIntegrated();
}

int main (int argc, char** argv)
{
    // 1. EBU Tech 3341: stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS (+-0.1).
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const double v = integratedOfSine (fs, 1000.0, -23.0, 20.0);
        char label[64]; std::snprintf (label, sizeof label, "Integrated, 1 kHz -23 dBFS stereo @ %.0f Hz", fs);
        check (std::abs (v + 23.0) < 0.1, label, v, -23.0);
    }
    {
        const double v = integratedOfSine (48000.0, 1000.0, -33.0, 20.0);
        check (std::abs (v + 33.0) < 0.1, "Integrated, 1 kHz -33 dBFS stereo", v, -33.0);
    }

    // 2. Gating: 20 s at -23 dBFS then 20 s at -80 dBFS (below absolute gate) -> still -23.
    {
        LoudnessMeter m; const double fs = 48000; m.prepare (fs);
        for (int part = 0; part < 2; ++part)
        {
            const double a = std::pow (10.0, (part == 0 ? -23.0 : -80.0) / 20.0);
            for (long i = 0; i < (long) (fs * 20); ++i) { const float s = (float) (a * std::sin (2 * pi * 1000 * i / fs)); m.processSample (s, s); }
        }
        check (std::abs (m.getIntegrated() + 23.0) < 0.1, "Integrated with silent tail (absolute gate)", m.getIntegrated(), -23.0);
    }

    // 3. LRA (EBU Tech 3342 case 1): 20 s at -20 dBFS then 20 s at -30 dBFS -> 10 LU (+-1).
    {
        LoudnessMeter m; const double fs = 48000; m.prepare (fs);
        for (int part = 0; part < 2; ++part)
        {
            const double a = std::pow (10.0, (part == 0 ? -20.0 : -30.0) / 20.0);
            for (long i = 0; i < (long) (fs * 20); ++i) { const float s = (float) (a * std::sin (2 * pi * 1000 * i / fs)); m.processSample (s, s); }
        }
        check (std::abs (m.getRange() - 10.0) < 1.0, "Loudness range, -20 then -30 dBFS", m.getRange(), 10.0);
    }

    // 4. True peak: sine at fs/4 with 45 deg phase -> sample peak -3 dB, true peak 0 dBTP.
    {
        TruePeakMeter tp; const double fs = 48000; float maxTp = 0, maxSample = 0;
        for (int i = 0; i < 48000; ++i)
        {
            const float s = (float) std::sin (2 * pi * (fs / 4) * i / fs + pi / 4);
            maxSample = std::max (maxSample, std::abs (s));
            maxTp = std::max (maxTp, tp.processSample (s, s));
        }
        check (std::abs (TruePeakMeter::toDb (maxSample) + 3.01) < 0.05, "Sample peak of fs/4 sine at 45 deg", TruePeakMeter::toDb (maxSample), -3.01);
        check (std::abs (TruePeakMeter::toDb (maxTp)) < 0.5, "True peak of the same sine", TruePeakMeter::toDb (maxTp), 0.0);
    }
    {
        TruePeakMeter tp; float maxTp = 0;
        for (int i = 0; i < 48000; ++i) { const float s = 0.5f * (float) std::sin (2 * pi * 997 * i / 48000.0); maxTp = std::max (maxTp, tp.processSample (s, s)); }
        check (std::abs (TruePeakMeter::toDb (maxTp) + 6.02) < 0.1, "True peak of 997 Hz sine at -6 dBFS", TruePeakMeter::toDb (maxTp), -6.02);
    }

    // 5. Band meter: 40 Hz sine lands in the sub band; anti-phase bass is detected.
    {
        BandMeter b; const double fs = 48000; b.prepare (fs);
        for (int i = 0; i < 48000; ++i) { const float s = 0.5f * (float) std::sin (2 * pi * 40 * i / fs); b.processSample (s, s); }
        auto snap = b.takeSnapshot();
        int loudest = 0; for (int i = 1; i < BandMeter::numBands; ++i) if (snap.bandDb[(size_t) i] > snap.bandDb[(size_t) loudest]) loudest = i;
        check (loudest == 0, "40 Hz sine -> loudest band is Sub", loudest, 0);
        check (std::abs (snap.bassCorrelation - 1.0) < 0.01, "Mono bass -> bass correlation +1", snap.bassCorrelation, 1.0);

        for (int i = 0; i < 48000; ++i) { const float s = 0.5f * (float) std::sin (2 * pi * 50 * i / fs); b.processSample (s, -s); }
        snap = b.takeSnapshot();
        check (snap.bassCorrelation < -0.95, "Anti-phase bass -> bass correlation -1", snap.bassCorrelation, -1.0);
    }

    // 6. Whole analyzer with a fake host: 174 BPM, 4/4. Bars, rules, section summaries.
    {
        MixAnalyzer an; const double fs = 48000; an.prepare (fs);
        Transport t; t.isPlaying = true; t.hasMusicalPosition = true; t.bpm = 174; t.ppqPosition = 64.0 * 4; // starts at bar 65
        const int block = 512; std::vector<float> L (block), R (block);
        long n = 0; int alerts = 0, clipAlerts = 0, bassAlerts = 0, summaries = 0, firstSummaryFrom = -1, firstSummaryTo = -1;
        for (int b = 0; b < (int) (fs * 40 / block); ++b)       // 40 s
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const double tsec = n / fs;
                const float kick = (float) (2.0 * std::sin (2 * pi * 55 * tsec));              // hot, clipping sub
                const float wide = (float) (0.8 * std::sin (2 * pi * 80 * tsec + 2.5));        // stereo bass, out of phase on R
                L[(size_t) i] = 0.5f * kick + wide; R[(size_t) i] = 0.5f * kick - wide;
            }
            an.process (L.data(), R.data(), block, t);
            t.ppqPosition += block * t.bpm / 60.0 / fs;
            Alert al; while (an.popAlert (al)) { ++alerts; std::printf ("     alert bar %d @%.1fs: %s\n", al.bar, al.seconds, al.text);
                                               if (al.severity == Severity::problem) ++clipAlerts;
                                               if (std::string (al.text).find ("120 Hz") != std::string::npos) ++bassAlerts; }
            SectionSummary s; while (an.popSummary (s)) { if (summaries++ == 0) { firstSummaryFrom = s.fromBar; firstSummaryTo = s.toBar; std::printf ("     summary: %s\n", s.toJson().c_str()); } }
        }
        const auto m = an.getMeters();
        check (m.bar >= 92 && m.bar <= 94, "Bar after 40 s at 174 BPM from bar 65", m.bar, 93);
        check (clipAlerts >= 1, "Clipping alert raised", clipAlerts, 1);
        check (bassAlerts == 1, "Stereo bass alert raised once (debounced)", bassAlerts, 1);
        check (firstSummaryFrom == 65 && firstSummaryTo == 72, "First summary covers bars 65-72", firstSummaryFrom * 100 + firstSummaryTo, 6572);
        check (summaries == 3, "Section summaries in 28 bars (8-bar sections)", summaries, 3);
        check (m.truePeakMaxDb > 0.0, "Max true peak above 0 dBTP", m.truePeakMaxDb, 1.0);

        // Stop resets nothing until the next play; next play starts fresh.
        t.isPlaying = false; an.process (L.data(), R.data(), block, t);
        t.isPlaying = true; t.ppqPosition = 0; an.process (L.data(), R.data(), block, t);
        check (an.getMeters().truePeakMaxDb < 0.0 || an.getMeters().seconds < 0.2, "New playback starts a fresh session", an.getMeters().seconds, 0.0);
    }

    // 7. Reference learning and deltas: learn pink-ish noise, then play with +6 dB sub.
    {
        MixAnalyzer an; const double fs = 48000; an.prepare (fs);
        Transport t; t.isPlaying = true; const int block = 480; std::vector<float> L (block), R (block);
        unsigned seed = 1; auto noise = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) / 8388608.0 - 1.0); };
        Biquad lp = Biquad::lowPass (fs, 50);
        auto render = [&] (double subGain, double seconds)
        {
            for (int b = 0; b < (int) (fs * seconds / block); ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    const double x = 0.1 * noise(); const double sub = lp.process (0.3 * noise()) * subGain;
                    L[(size_t) i] = R[(size_t) i] = (float) (x + sub);
                }
                an.process (L.data(), R.data(), block, t);
                Alert a; while (an.popAlert (a)) {}
                SectionSummary s; while (an.popSummary (s)) {}
            }
        };
        an.startLearning(); render (1.0, 20.0);
        auto ref = an.stopLearning ("test");
        check (ref.valid, "Reference learned from 20 s", ref.valid, 1);
        t.isPlaying = false; an.process (L.data(), R.data(), block, t);
        an.setReference (ref);
        t.isPlaying = true; render (2.0, 15.0);
        const auto m = an.getMeters();
        check (m.deltaDb[0] > 3.0 && m.deltaDb[0] < 7.0, "Sub +6 dB louder -> positive delta to reference", m.deltaDb[0], 4.5);
        check (std::abs (m.deltaDb[3]) < 1.5, "Mids unchanged -> delta near 0", m.deltaDb[3], 0.0);
    }

    // 8. Spectrum: full-scale 1 kHz sine, no tilt -> peak near 0 dB at 1 kHz, low elsewhere.
    {
        SpectrumAnalyzer sp; sp.setSampleRate (48000); sp.setTilt (false);
        std::vector<float> s (SpectrumAnalyzer::fftSize);
        for (int i = 0; i < (int) s.size(); ++i) s[(size_t) i] = (float) std::sin (2 * pi * 1000 * i / 48000.0);
        sp.push (s.data(), s.data(), (int) s.size());
        for (int k = 0; k < 40; ++k) sp.update (1.0 / 30, true);
        int best = 0; for (int p = 0; p < SpectrumAnalyzer::numPoints; ++p) if (sp.getAverage()[(size_t) p] > sp.getAverage()[(size_t) best]) best = p;
        const double f = SpectrumAnalyzer::frequencyOf (best);
        check (std::abs (std::log2 (f / 1000.0)) < 0.1, "Spectrum peak frequency for 1 kHz sine", f, 1000.0);
        check (sp.getAverage()[(size_t) best] > -20.0 && sp.getAverage()[(size_t) best] < 1.0, "Sine level after 1/6-oct smoothing (spread)", sp.getAverage()[(size_t) best], -12.0);
        int p100 = 0; while (SpectrumAnalyzer::frequencyOf (p100) < 100) ++p100;
        check (sp.getAverage()[(size_t) p100] < -80.0, "Spectrum at 100 Hz far below", sp.getAverage()[(size_t) p100], -100.0);
    }

    // 9. Width contrast: 16 bars wide & quiet breakdown, then a loud mono drop -> one info alert at the drop.
    {
        MixAnalyzer an; const double fs = 48000; an.prepare (fs);
        Transport t; t.isPlaying = true; t.hasMusicalPosition = true; t.bpm = 174; t.ppqPosition = 16.0 * 4;  // bar 17
        const int block = 480; std::vector<float> L (block), R (block);
        unsigned seed = 3; auto noise = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) / 8388608.0 - 1.0); };
        int widthAlerts = 0, widthBar = 0; long n = 0;
        const double barSec = 4 * 60.0 / 174;
        for (int b = 0; b < (int) (fs * barSec * 26 / block); ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const bool drop = n / fs >= barSec * 16;
                const double a = noise(), c = noise();
                L[(size_t) i] = (float) (drop ? 0.5 * a : 0.08 * a);
                R[(size_t) i] = (float) (drop ? 0.5 * a : 0.08 * c);           // breakdown: decorrelated = wide
            }
            an.process (L.data(), R.data(), block, t);
            t.ppqPosition += block * t.bpm / 60.0 / fs;
            Alert al; while (an.popAlert (al)) if (std::string (al.text).find ("mono (šírka") != std::string::npos) { ++widthAlerts; widthBar = al.bar; std::printf ("     alert bar %d: %s\n", al.bar, al.text); }
            SectionSummary s; while (an.popSummary (s)) {}
        }
        check (widthAlerts == 1, "Width contrast alert once", widthAlerts, 1);
        check (widthBar == 33, "Width contrast alert at the drop (bar 33)", widthBar, 33);
    }

    // 10. Premaster vs master: PSR 7 dB warns in premaster (limit 8), not in master (limit 6).
    const double clipAt = argc > 1 ? std::atof (argv[1]) : 4.5;
    for (int modeIdx = 0; modeIdx < 2; ++modeIdx)
    {
        MixAnalyzer an; const double fs = 48000; an.prepare (fs);
        an.setMode (modeIdx == 0 ? MasterMode::premaster : MasterMode::master);
        Transport t; t.isPlaying = true; const int block = 480; std::vector<float> L (block), R (block);
        int psrAlerts = 0; long n = 0;
        // Gaussian noise hard-clipped: PSR ends up between 6 and 8 dB
        unsigned seed = 11; auto uni = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) / 8388608.0 - 1.0); };
        for (int b = 0; b < (int) (fs * 12 / block); ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const double u1 = 0.5 * (uni() + 1.0) + 1e-12, u2 = 0.5 * (uni() + 1.0);
                const double g = std::sqrt (-2.0 * std::log (u1)) * std::cos (2 * pi * u2);   // Box-Muller
                L[(size_t) i] = R[(size_t) i] = (float) (0.1 * std::clamp (g, -clipAt, clipAt));
            }
            an.process (L.data(), R.data(), block, t);
            Alert al; while (an.popAlert (al)) if (std::string (al.text).find ("PSR") != std::string::npos) ++psrAlerts;
            SectionSummary s; while (an.popSummary (s)) {}
        }
        const double psr = an.getMeters().psrDb;
        char label[96]; std::snprintf (label, sizeof label, "%s: PSR %.1f dB -> alert %s", modeIdx == 0 ? "Premaster" : "Master", psr, modeIdx == 0 ? "yes" : "no");
        const bool ok = psr > 6.0 && psr < 8.0 ? (modeIdx == 0 ? psrAlerts == 1 : psrAlerts == 0) : false;
        check (ok, label, psrAlerts, modeIdx == 0 ? 1 : 0);
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
