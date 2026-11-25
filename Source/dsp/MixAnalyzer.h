#pragma once
#include "LoudnessMeter.h"
#include "TruePeakMeter.h"
#include "BandMeter.h"
#include "SpscQueue.h"
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

namespace kmf
{
// Everything the plugin measures, in one place. Pure C++ (no JUCE), so it can be
// unit-tested on its own. process() runs on the audio thread and never allocates.

struct Transport
{
    bool   isPlaying = false;
    bool   hasMusicalPosition = false;
    double ppqPosition = 0.0;      // at the first sample of the block
    double bpm = 174.0;
    int    timeSigNum = 4, timeSigDen = 4;
};

// Reference: band balance and dynamics learned from a reference track.
// Band balance = each band's level relative to the momentary K-weighted loudness,
// so the overall volume of the reference does not matter.
struct ReferenceProfile
{
    bool valid = false;
    std::string name;
    std::array<double, BandMeter::numBands> balanceDb {};
    double shortTermLufs = 0.0;    // mean short-term loudness while playing
    double psrDb = 0.0;            // mean PSR

    std::string toJson() const
    {
        std::string s = "{\"name\":\"" + escape (name) + "\",\"balanceDb\":[";
        for (int i = 0; i < BandMeter::numBands; ++i)
            s += (i ? "," : "") + num (balanceDb[(size_t) i]);
        s += "],\"shortTermLufs\":" + num (shortTermLufs) + ",\"psrDb\":" + num (psrDb) + "}";
        return s;
    }

private:
    static std::string num (double v) { char b[32]; std::snprintf (b, sizeof b, "%.2f", v); return b; }
    static std::string escape (const std::string& in)
    {
        std::string o;
        for (char c : in) { if (c == '"' || c == '\\') o += '\\'; o += c; }
        return o;
    }
};

enum class Severity { info, warning, problem };

struct Alert
{
    Severity severity = Severity::info;
    int  bar = 0;                  // 0 = no musical position from the host
    double seconds = 0.0;          // since playback started
    char text[256] {};
};

// Statistics over one section (8 bars, or 16 s without tempo). Payload for the AI layer.
struct SectionSummary
{
    int fromBar = 0, toBar = 0;
    double fromSec = 0.0, toSec = 0.0;
    double shortTermLufs = -200.0, psrDb = 0.0, maxTruePeakDb = -200.0;
    double correlation = 1.0, bassCorrelation = 1.0, width = 0.0;
    std::array<double, BandMeter::numBands> balanceDb {};
    std::array<double, BandMeter::numBands> deltaToReferenceDb {};   // 0 when no reference
    bool hasReference = false;

    std::string toJson() const
    {
        auto n = [] (double v) { char b[32]; std::snprintf (b, sizeof b, "%.1f", v); return std::string (b); };
        std::string s = "{\"bars\":[" + std::to_string (fromBar) + "," + std::to_string (toBar) + "]"
                      + ",\"seconds\":[" + n (fromSec) + "," + n (toSec) + "]"
                      + ",\"shortTermLufs\":" + n (shortTermLufs) + ",\"psrDb\":" + n (psrDb)
                      + ",\"maxTruePeakDb\":" + n (maxTruePeakDb) + ",\"correlation\":" + n (correlation)
                      + ",\"bassCorrelation\":" + n (bassCorrelation) + ",\"width\":" + n (width) + ",\"bands\":{";
        for (int i = 0; i < BandMeter::numBands; ++i)
        {
            s += std::string (i ? "," : "") + "\"" + BandMeter::bandNames[i] + "\":{\"balanceDb\":" + n (balanceDb[(size_t) i]);
            if (hasReference) s += ",\"deltaToReferenceDb\":" + n (deltaToReferenceDb[(size_t) i]);
            s += "}";
        }
        return s + "}}";
    }
};

class MixAnalyzer
{
public:
    // Rule thresholds. Starting points, meant to be tuned by ear on your own music.
    struct Thresholds
    {
        double truePeakWarnDb = -1.0;     // above this: warning
        double truePeakClipDb = 0.0;      // above this: problem
        double psrLowDb = 6.0;            // short-term PSR below this for psrHoldSec: "too compressed"
        double psrHoldSec = 4.0;
        double bassCorrMin = 0.7;         // below 120 Hz correlation lower than this ...
        double bassHoldSec = 2.0;         // ... for this long: "bass not mono"
        double bassMinLevelDb = -45.0;    // ignore when there is hardly any bass
        double phaseCorrMin = 0.0;        // full-band correlation below 0: phase problem
        double bandDeltaDb = 3.0;         // band off the reference by more than this over a section
    } thresholds;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        loudness.prepare (fs);
        bands.prepare (fs);
        truePeak.reset();
        resetSession (0.0);
    }

    // ---------------- audio thread ----------------
    void process (const float* left, const float* right, int numSamples, const Transport& t) noexcept
    {
        if (t.isPlaying && ! wasPlaying.load())
            resetSession (t.ppqPosition);          // every new playback starts a fresh measurement
        wasPlaying.store (t.isPlaying);
        if (! t.isPlaying)
            return;                                // measure only while the DAW plays

        transport = t;
        const double quarterNotesPerBar = 4.0 * t.timeSigNum / std::max (1, t.timeSigDen);
        const double ppqPerSample = t.bpm / 60.0 / fs;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i], r = right != nullptr ? right[i] : left[i];
            const float tp = truePeak.processSample (l, r);
            stepTruePeak = std::max (stepTruePeak, tp);
            sessionTruePeak = std::max (sessionTruePeak, tp);
            bands.processSample (l, r);
            ++samplesSinceStart;

            if (loudness.processSample (l, r))
            {
                const double ppq = t.ppqPosition + i * ppqPerSample;
                currentBar = t.hasMusicalPosition ? (int) std::floor (ppq / quarterNotesPerBar) + 1 : 0;
                onStepFinished();
            }
        }
    }

    // ---------------- UI thread ----------------
    struct Meters
    {
        double momentary, shortTerm, integrated, lra;
        double truePeakDb, truePeakMaxDb, plrDb, psrDb;
        double correlation, bassCorrelation, width;
        std::array<double, BandMeter::numBands> balanceDb, deltaDb;
        bool hasReference, playing, learning;
        int bar;
        double seconds;
    };

    Meters getMeters() const noexcept
    {
        Meters m {};
        m.momentary = a.momentary.load(); m.shortTerm = a.shortTerm.load();
        m.integrated = a.integrated.load(); m.lra = a.lra.load();
        m.truePeakDb = a.truePeak.load(); m.truePeakMaxDb = a.truePeakMax.load();
        m.plrDb = a.plr.load(); m.psrDb = a.psr.load();
        m.correlation = a.correlation.load(); m.bassCorrelation = a.bassCorrelation.load(); m.width = a.width.load();
        for (int i = 0; i < BandMeter::numBands; ++i)
        {
            m.balanceDb[(size_t) i] = a.balance[(size_t) i].load();
            m.deltaDb[(size_t) i]   = a.delta[(size_t) i].load();
        }
        m.hasReference = referenceValid.load(); m.playing = wasPlaying.load(); m.learning = learning.load();
        m.bar = a.bar.load(); m.seconds = a.seconds.load();
        return m;
    }

    bool popAlert (Alert& out) noexcept            { return alerts.pop (out); }
    bool popSummary (SectionSummary& out) noexcept { return summaries.pop (out); }

    // Reference handling. Safe to call from the UI thread at any time.
    void setReference (const ReferenceProfile& p) noexcept
    {
        referenceValid.store (false);
        for (int i = 0; i < BandMeter::numBands; ++i) refBalance[(size_t) i].store (p.balanceDb[(size_t) i]);
        referenceValid.store (p.valid);
    }

    void startLearning() noexcept { learnSteps = 0; learnBalance.fill (0.0); learnLufs = learnPsr = 0.0; learning.store (true); }

    // Ends learning and returns the averaged profile (valid only if enough music was heard).
    ReferenceProfile stopLearning (const std::string& name) noexcept
    {
        learning.store (false);
        ReferenceProfile p;
        p.name = name;
        if (learnSteps >= 100)                             // at least 10 s of audio
        {
            for (int i = 0; i < BandMeter::numBands; ++i)
                p.balanceDb[(size_t) i] = learnBalance[(size_t) i] / learnSteps;
            p.shortTermLufs = learnLufs / learnSteps;
            p.psrDb = learnPsr / learnSteps;
            p.valid = true;
        }
        return p;
    }

private:
    void resetSession (double ppqStart) noexcept
    {
        loudness.reset();
        truePeak.reset();
        bands.reset();
        samplesSinceStart = 0;
        stepTruePeak = sessionTruePeak = 0.0f;
        tpRing.fill (0.0f); tpRingPos = 0;
        balanceSmooth.fill (-200.0); smoothInit = false;
        psrLowSince = bassBadSince = -1.0;
        tpWarned = tpClipped = psrWarned = bassWarned = phaseWarned = false;
        sectionStartSec = 0.0; sectionStartBar = 0; sectionSteps = 0;
        sectionAccum = {};
        currentBar = 0;
        (void) ppqStart;
        publishSilence();
    }

    void onStepFinished() noexcept
    {
        const double sec = samplesSinceStart / fs;
        const auto snap = bands.takeSnapshot();

        // True peak over the last 3 s for PSR (short-term peak-to-loudness ratio).
        tpRing[(size_t) tpRingPos] = stepTruePeak; tpRingPos = (tpRingPos + 1) % (int) tpRing.size();
        float tp3s = 0.0f; for (float v : tpRing) tp3s = std::max (tp3s, v);
        stepTruePeak = 0.0f;

        const double st = loudness.getShortTerm(), integ = loudness.getIntegrated();
        const double tpDb = TruePeakMeter::toDb (tp3s), tpMaxDb = TruePeakMeter::toDb (sessionTruePeak);
        const double psr = st > LoudnessMeter::silence + 1 ? tpDb - st : 0.0;
        const double plr = integ > LoudnessMeter::silence + 1 ? tpMaxDb - integ : 0.0;

        // Band balance: each band's level relative to the K-weighted (perceived) loudness of the
        // same moment, smoothed over ~3 s. This is a loudness-matched comparison: a reference
        // mastered louder or quieter compares fairly, and boosting the sub (which K-weighting
        // mostly ignores) does not drag the other bands down.
        const double mean = loudness.getMomentary();
        const bool audible = st > -60.0 && mean > -70.0;
        std::array<double, BandMeter::numBands> balance {}, delta {};
        for (int i = 0; i < BandMeter::numBands; ++i)
        {
            const double b = snap.bandDb[(size_t) i] - mean;
            balanceSmooth[(size_t) i] = smoothInit ? balanceSmooth[(size_t) i] + 0.033 * (b - balanceSmooth[(size_t) i]) : b;
            balance[(size_t) i] = balanceSmooth[(size_t) i];
            delta[(size_t) i] = referenceValid.load() ? balance[(size_t) i] - refBalance[(size_t) i].load() : 0.0;
        }
        if (audible) smoothInit = true;

        if (learning.load() && audible)
        {
            for (int i = 0; i < BandMeter::numBands; ++i) learnBalance[(size_t) i] += snap.bandDb[(size_t) i] - mean;
            learnLufs += st; learnPsr += psr; ++learnSteps;
        }

        // Publish for the UI.
        a.momentary.store (loudness.getMomentary()); a.shortTerm.store (st);
        a.integrated.store (integ); a.lra.store (loudness.getRange());
        a.truePeak.store (tpDb); a.truePeakMax.store (tpMaxDb); a.plr.store (plr); a.psr.store (psr);
        a.correlation.store (snap.correlation); a.bassCorrelation.store (snap.bassCorrelation); a.width.store (snap.width);
        for (int i = 0; i < BandMeter::numBands; ++i) { a.balance[(size_t) i].store (balance[(size_t) i]); a.delta[(size_t) i].store (delta[(size_t) i]); }
        a.bar.store (currentBar); a.seconds.store (sec);

        if (audible && ! learning.load())
            evaluateRules (sec, tpDb, tpMaxDb, psr, snap);

        accumulateSection (sec, st, psr, tpDb, snap, balance, delta, audible);
    }

    void evaluateRules (double sec, double tpDb, double tpMaxDb, double psr, const BandMeter::Snapshot& snap) noexcept
    {
        const auto& th = thresholds;
        char msg[256];

        if (tpMaxDb > th.truePeakClipDb && ! tpClipped)
        {
            std::snprintf (msg, sizeof msg, "True peak %+.1f dBTP – signál clipuje. Zníž ceiling limitera aspoň na -1 dBTP.", tpMaxDb);
            post (Severity::problem, sec, msg); tpClipped = tpWarned = true;
        }
        else if (tpDb > th.truePeakWarnDb && ! tpWarned)
        {
            std::snprintf (msg, sizeof msg, "True peak %+.1f dBTP je nad -1 dBTP. Pri konverzii do MP3/AAC môže skresliť.", tpDb);
            post (Severity::warning, sec, msg); tpWarned = true;
        }

        if (psr < th.psrLowDb)
        {
            if (psrLowSince < 0) psrLowSince = sec;
            if (! psrWarned && sec - psrLowSince >= th.psrHoldSec)
            {
                std::snprintf (msg, sizeof msg, "PSR %.1f dB už %.0f s – táto časť je silno stlačená, tranzienty sa strácajú.", psr, sec - psrLowSince);
                post (Severity::warning, sec, msg); psrWarned = true;
            }
        }
        else if (psr > th.psrLowDb + 1.0) { psrLowSince = -1.0; psrWarned = false; }

        if (snap.bassLevelDb > th.bassMinLevelDb && snap.bassCorrelation < th.bassCorrMin)
        {
            if (bassBadSince < 0) bassBadSince = sec;
            if (! bassWarned && sec - bassBadSince >= th.bassHoldSec)
            {
                std::snprintf (msg, sizeof msg, "Bas pod 120 Hz nie je mono (korelácia %.2f). Daj sub a bas do mona.", snap.bassCorrelation);
                post (Severity::warning, sec, msg); bassWarned = true;
            }
        }
        else if (snap.bassCorrelation > th.bassCorrMin + 0.1) { bassBadSince = -1.0; bassWarned = false; }

        if (snap.correlation < th.phaseCorrMin && ! phaseWarned)
        {
            std::snprintf (msg, sizeof msg, "Záporná korelácia (%.2f) – fázový problém, v mone sa časť mixu vyruší.", snap.correlation);
            post (Severity::problem, sec, msg); phaseWarned = true;
        }
        else if (snap.correlation > 0.2) phaseWarned = false;
    }

    void accumulateSection (double sec, double st, double psr, double tpDb, const BandMeter::Snapshot& snap,
                            const std::array<double, BandMeter::numBands>& balance,
                            const std::array<double, BandMeter::numBands>& delta, bool audible) noexcept
    {
        if (sectionSteps == 0) { sectionStartSec = sec; sectionStartBar = currentBar; }
        if (audible)
        {
            auto& s = sectionAccum;
            s.energy += std::pow (10.0, st / 10.0); s.psr += psr; s.corr += snap.correlation;
            s.bassCorr += snap.bassCorrelation; s.width += snap.width; s.maxTp = std::max (s.maxTp, tpDb);
            for (int i = 0; i < BandMeter::numBands; ++i) { s.balance[(size_t) i] += balance[(size_t) i]; s.delta[(size_t) i] += delta[(size_t) i]; }
            ++s.n;
        }
        ++sectionSteps;

        const bool hasBars = currentBar > 0 && sectionStartBar > 0;
        const bool done = hasBars ? (currentBar - sectionStartBar >= 8) : (sec - sectionStartSec >= 16.0);
        if (! done) return;

        const auto& s = sectionAccum;
        if (s.n > 0)
        {
            SectionSummary out;
            out.fromBar = sectionStartBar; out.toBar = hasBars ? currentBar - 1 : 0;
            out.fromSec = sectionStartSec; out.toSec = sec;
            out.shortTermLufs = 10.0 * std::log10 (s.energy / s.n);
            out.psrDb = s.psr / s.n; out.maxTruePeakDb = s.maxTp;
            out.correlation = s.corr / s.n; out.bassCorrelation = s.bassCorr / s.n; out.width = s.width / s.n;
            out.hasReference = referenceValid.load();
            for (int i = 0; i < BandMeter::numBands; ++i)
            {
                out.balanceDb[(size_t) i] = s.balance[(size_t) i] / s.n;
                out.deltaToReferenceDb[(size_t) i] = s.delta[(size_t) i] / s.n;
            }
            summaries.push (out);

            // Section-level rule: a band clearly off the reference for the whole section.
            if (out.hasReference)
                for (int i = 0; i < BandMeter::numBands; ++i)
                {
                    const double d = out.deltaToReferenceDb[(size_t) i];
                    if (std::abs (d) >= thresholds.bandDeltaDb)
                    {
                        char msg[256];
                        std::snprintf (msg, sizeof msg, "%s je o %+.1f dB %s referenciou (celý úsek).", BandMeter::bandNames[i],
                                       d, d > 0 ? "nad" : "pod");
                        post (Severity::info, sec, msg, out.fromBar);
                    }
                }
        }
        sectionSteps = 0;
        sectionAccum = {};
    }

    void post (Severity sev, double sec, const char* text, int bar = -1) noexcept
    {
        Alert al;
        al.severity = sev; al.bar = bar >= 0 ? bar : currentBar; al.seconds = sec;
        std::snprintf (al.text, sizeof al.text, "%s", text);
        alerts.push (al);
    }

    void publishSilence() noexcept
    {
        const double s = LoudnessMeter::silence;
        a.momentary.store (s); a.shortTerm.store (s); a.integrated.store (s); a.lra.store (0);
        a.truePeak.store (s); a.truePeakMax.store (s); a.plr.store (0); a.psr.store (0);
        a.correlation.store (1); a.bassCorrelation.store (1); a.width.store (0);
        for (auto& v : a.balance) v.store (0);
        for (auto& v : a.delta) v.store (0);
        a.bar.store (0); a.seconds.store (0);
    }

    double fs = 48000.0;
    LoudnessMeter loudness;
    TruePeakMeter truePeak;
    BandMeter bands;
    Transport transport;
    std::atomic<bool> wasPlaying { false };
    long long samplesSinceStart = 0;
    int currentBar = 0;

    float stepTruePeak = 0.0f, sessionTruePeak = 0.0f;
    std::array<float, 30> tpRing {}; int tpRingPos = 0;
    std::array<double, BandMeter::numBands> balanceSmooth {}; bool smoothInit = false;

    double psrLowSince = -1, bassBadSince = -1;
    bool tpWarned = false, tpClipped = false, psrWarned = false, bassWarned = false, phaseWarned = false;

    struct SectionAccum
    {
        double energy = 0, psr = 0, corr = 0, bassCorr = 0, width = 0, maxTp = -200;
        std::array<double, BandMeter::numBands> balance {}, delta {};
        long n = 0;
    } sectionAccum;
    double sectionStartSec = 0; int sectionStartBar = 0; long sectionSteps = 0;

    std::array<std::atomic<double>, BandMeter::numBands> refBalance {};
    std::atomic<bool> referenceValid { false }, learning { false };
    std::array<double, BandMeter::numBands> learnBalance {}; double learnLufs = 0, learnPsr = 0; long learnSteps = 0;

    struct Published
    {
        std::atomic<double> momentary { -200 }, shortTerm { -200 }, integrated { -200 }, lra { 0 };
        std::atomic<double> truePeak { -200 }, truePeakMax { -200 }, plr { 0 }, psr { 0 };
        std::atomic<double> correlation { 1 }, bassCorrelation { 1 }, width { 0 };
        std::array<std::atomic<double>, BandMeter::numBands> balance {}, delta {};
        std::atomic<int> bar { 0 };
        std::atomic<double> seconds { 0 };
    } a;

    SpscQueue<Alert, 256> alerts;
    SpscQueue<SectionSummary, 64> summaries;
};
} // namespace kmf
