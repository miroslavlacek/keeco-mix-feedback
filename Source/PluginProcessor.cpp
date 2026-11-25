#include "PluginProcessor.h"
#include "PluginEditor.h"

KeecoMixFeedbackProcessor::KeecoMixFeedbackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    link = std::make_unique<McpLinkServer> ([this] (const juce::String& req) { return handleLinkRequest (req); });
    startTimerHz (10);
}

KeecoMixFeedbackProcessor::~KeecoMixFeedbackProcessor()
{
    alive->store (false);
    stopTimer();
    link.reset();
}

bool KeecoMixFeedbackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void KeecoMixFeedbackProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    const auto mode = analyzer.getMode();
    analyzer.prepare (sampleRate);
    analyzer.setMode (mode);
    if (reference.profile.valid)
        analyzer.setReference (reference.profile);
    spectrumFifo.reset();
}

void KeecoMixFeedbackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    kmf::Transport t;
    if (auto* host = getPlayHead())
    {
        if (const auto pos = host->getPosition())
        {
            t.isPlaying = pos->getIsPlaying();
            if (const auto ppq = pos->getPpqPosition())   { t.ppqPosition = *ppq; t.hasMusicalPosition = true; }
            if (const auto bpm = pos->getBpm())           t.bpm = *bpm;
            if (const auto sig = pos->getTimeSignature()) { t.timeSigNum = sig->numerator; t.timeSigDen = sig->denominator; }
        }
    }
    else
    {
        t.isPlaying = true;   // standalone app: always measuring
    }
    hostBpm.store (t.bpm); hostSigNum.store (t.timeSigNum); hostSigDen.store (t.timeSigDen);
    hostHasPosition.store (t.hasMusicalPosition);

    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
    analyzer.process (left, right, n, t);

    if (t.isPlaying)
    {
        int s1, n1, s2, n2;
        spectrumFifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 > 0) { spectrumBuffer.copyFrom (0, s1, left, n1);      spectrumBuffer.copyFrom (1, s1, right, n1); }
        if (n2 > 0) { spectrumBuffer.copyFrom (0, s2, left + n1, n2); spectrumBuffer.copyFrom (1, s2, right + n1, n2); }
        spectrumFifo.finishedWrite (n1 + n2);
    }
    // Audio passes through untouched: this plugin only listens.
}

int KeecoMixFeedbackProcessor::readSpectrumAudio (float* left, float* right, int maxSamples)
{
    int s1, n1, s2, n2;
    spectrumFifo.prepareToRead (juce::jmin (maxSamples, spectrumFifo.getNumReady()), s1, n1, s2, n2);
    if (n1 > 0) { juce::FloatVectorOperations::copy (left, spectrumBuffer.getReadPointer (0, s1), n1);
                  juce::FloatVectorOperations::copy (right, spectrumBuffer.getReadPointer (1, s1), n1); }
    if (n2 > 0) { juce::FloatVectorOperations::copy (left + n1, spectrumBuffer.getReadPointer (0, s2), n2);
                  juce::FloatVectorOperations::copy (right + n1, spectrumBuffer.getReadPointer (1, s2), n2); }
    spectrumFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

// ------------------------------------------------------------------ log & sections

void KeecoMixFeedbackProcessor::timerCallback()
{
    const bool playing = analyzer.getMeters().playing;
    if (playing && ! wasPlaying)
    {
        const juce::ScopedLock sl (logLock);   // new playback = new session
        log.clear();
        sections.clear();
    }
    wasPlaying = playing;

    kmf::Alert a;
    while (analyzer.popAlert (a))
        addLog ({ a.severity, a.bar, a.seconds, juce::String::fromUTF8 (a.text), Source::rules });

    kmf::SectionSummary s;
    while (analyzer.popSummary (s))
    {
        const juce::ScopedLock sl (logLock);
        sections.push_back ({ s.fromBar, s.toBar, juce::String::fromUTF8 (s.toJson().c_str()) });
        if (sections.size() > 400) sections.erase (sections.begin());
    }
}

void KeecoMixFeedbackProcessor::addLog (LogItem item)
{
    const juce::ScopedLock sl (logLock);
    log.push_back (std::move (item));
    if (log.size() > 300) log.erase (log.begin());
}

void KeecoMixFeedbackProcessor::addNote (const juce::String& text, kmf::Severity s)
{
    const auto m = analyzer.getMeters();
    addLog ({ s, 0, m.playing ? m.seconds : 0.0, text, Source::plugin });
}

void KeecoMixFeedbackProcessor::clearLog()
{
    const juce::ScopedLock sl (logLock);
    log.clear();
}

std::vector<KeecoMixFeedbackProcessor::LogItem> KeecoMixFeedbackProcessor::getLogNewestFirst (size_t maxItems) const
{
    const juce::ScopedLock sl (logLock);
    std::vector<LogItem> out;
    for (auto it = log.rbegin(); it != log.rend() && out.size() < maxItems; ++it) out.push_back (*it);
    return out;
}

int KeecoMixFeedbackProcessor::getSectionCount() const
{
    const juce::ScopedLock sl (logLock);
    return (int) sections.size();
}

juce::String KeecoMixFeedbackProcessor::getSectionsJson (int fromBar, int toBar) const
{
    const juce::ScopedLock sl (logLock);
    juce::StringArray parts;
    for (const auto& s : sections)
        if (s.toBar == 0 || (s.toBar >= fromBar && s.fromBar <= toBar))
            parts.add (s.json);
    return "[" + parts.joinIntoString (",") + "]";
}

// ------------------------------------------------------------------ reference & state

void KeecoMixFeedbackProcessor::applyReference (const StoredReference& r)
{
    reference = r;
    analyzer.setReference (r.profile);
    const juce::ScopedLock sl (refLock);
    referenceName = juce::String::fromUTF8 (r.profile.name.c_str());
}

void KeecoMixFeedbackProcessor::clearReference()
{
    reference = {};
    analyzer.setReference ({});
    const juce::ScopedLock sl (refLock);
    referenceName.clear();
}

void KeecoMixFeedbackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("KeecoMixFeedback");
    if (reference.profile.valid)
        state.setProperty ("reference", ReferenceStore::toJson (reference), nullptr);
    state.setProperty ("mode", analyzer.getMode() == kmf::MasterMode::master ? "master" : "premaster", nullptr);
    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void KeecoMixFeedbackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid()) return;
    StoredReference r;
    if (ReferenceStore::fromJson (state.getProperty ("reference").toString(), r))
        applyReference (r);
    analyzer.setMode (state.getProperty ("mode").toString() == "master" ? kmf::MasterMode::master : kmf::MasterMode::premaster);
}

// ------------------------------------------------------------------ MCP bridge requests

namespace
{
juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& p : props) o->setProperty (p.first, p.second);
    return juce::var (o);
}

juce::var num (double v, int decimals = 1)
{
    if (v <= -150.0) return juce::var();          // silence / not measured -> null
    const double f = std::pow (10.0, decimals);
    return std::round (v * f) / f;
}

juce::String reply (const juce::var& v)    { return juce::JSON::toString (obj ({ { "ok", true }, { "result", v } }), true); }
juce::String failure (const juce::String& m) { return juce::JSON::toString (obj ({ { "ok", false }, { "error", m } }), true); }

const char* severityName (kmf::Severity s) { return s == kmf::Severity::problem ? "problem" : s == kmf::Severity::warning ? "warning" : "info"; }
}

juce::String KeecoMixFeedbackProcessor::statusJson() const
{
    const auto m = analyzer.getMeters();
    juce::Array<juce::var> bands;
    for (int i = 0; i < kmf::BandMeter::numBands; ++i)
    {
        auto b = obj ({ { "band", juce::String::fromUTF8 (kmf::BandMeter::bandNames[i]) },
                        { "balanceDb", num (m.balanceDb[(size_t) i]) } });
        if (m.hasReference) b.getDynamicObject()->setProperty ("deltaToReferenceDb", num (m.deltaDb[(size_t) i]));
        bands.add (b);
    }
    juce::String refName;
    { const juce::ScopedLock sl (refLock); refName = referenceName; }

    return reply (obj ({
        { "playing", m.playing },
        { "bar", hostHasPosition.load() ? juce::var (m.bar) : juce::var() },
        { "secondsSincePlay", num (m.seconds) },
        { "bpm", num (hostBpm.load(), 2) },
        { "timeSignature", juce::String (hostSigNum.load()) + "/" + juce::String (hostSigDen.load()) },
        { "mode", analyzer.getMode() == kmf::MasterMode::master ? "master" : "premaster" },
        { "reference", m.hasReference ? juce::var (refName) : juce::var() },
        { "loudness", obj ({ { "momentaryLufs", num (m.momentary) }, { "shortTermLufs", num (m.shortTerm) },
                             { "integratedLufs", num (m.integrated) }, { "lraLu", num (m.lra) } }) },
        { "peaks", obj ({ { "truePeakLast3sDbtp", num (m.truePeakDb) }, { "truePeakMaxDbtp", num (m.truePeakMaxDb) } }) },
        { "dynamics", obj ({ { "plrDb", num (m.plrDb) }, { "psrDb", num (m.psrDb) } }) },
        { "stereo", obj ({ { "correlation", num (m.correlation, 2) }, { "bassCorrelationBelow120Hz", num (m.bassCorrelation, 2) },
                           { "widthSideShare", num (m.width, 2) } }) },
        { "bands", bands },
        { "sectionsCollected", getSectionCount() },
        { "note", "balanceDb = band level relative to momentary K-weighted loudness (loudness-matched). "
                  "Sub reads high by nature because K-weighting ignores it; judge bands against a reference." } }));
}

juce::String KeecoMixFeedbackProcessor::handleLinkRequest (const juce::String& json)
{
    const auto req = juce::JSON::parse (json);
    const auto cmd = req["cmd"].toString();
    const auto args = req["args"];

    if (cmd == "status")
        return statusJson();

    if (cmd == "sections")
    {
        const int from = args.hasProperty ("from_bar") ? (int) args["from_bar"] : 0;
        const int to   = args.hasProperty ("to_bar")   ? (int) args["to_bar"]   : (1 << 30);
        return reply (juce::JSON::parse (getSectionsJson (from, to)));
    }

    if (cmd == "alerts")
    {
        const int limit = juce::jlimit (1, 200, args.hasProperty ("limit") ? (int) args["limit"] : 30);
        juce::Array<juce::var> items;
        for (const auto& l : getLogNewestFirst ((size_t) limit))
            items.add (obj ({ { "severity", severityName (l.severity) }, { "bar", l.bar > 0 ? juce::var (l.bar) : juce::var() },
                              { "secondsSincePlay", num (l.seconds) }, { "text", l.text },
                              { "source", l.source == Source::claude ? "claude" : l.source == Source::rules ? "rules" : "plugin" } }));
        return reply (items);
    }

    if (cmd == "post_insight")
    {
        const auto text = args["text"].toString().trim().substring (0, 600);
        if (text.isEmpty()) return failure ("text is empty");
        const auto m = analyzer.getMeters();
        const int bar = args.hasProperty ("bar") ? (int) args["bar"] : (m.playing ? m.bar : 0);
        const auto sev = args["severity"].toString() == "warning" ? kmf::Severity::warning
                       : args["severity"].toString() == "problem" ? kmf::Severity::problem : kmf::Severity::info;
        addLog ({ sev, bar, m.playing ? m.seconds : 0.0, text, Source::claude });
        return reply ("posted");
    }

    if (cmd == "set_mode")
    {
        const auto mode = args["mode"].toString();
        if (mode != "premaster" && mode != "master") return failure ("mode must be premaster or master");
        analyzer.setMode (mode == "master" ? kmf::MasterMode::master : kmf::MasterMode::premaster);
        return reply ("mode set to " + mode);
    }

    if (cmd == "list_references")
    {
        juce::Array<juce::var> names;
        for (const auto& n : ReferenceStore::listNames()) names.add (n);
        juce::String current;
        { const juce::ScopedLock sl (refLock); current = referenceName; }
        return reply (obj ({ { "available", names }, { "active", current.isEmpty() ? juce::var() : juce::var (current) } }));
    }

    if (cmd == "set_reference")
    {
        const auto name = args["name"].toString();
        if (name.isEmpty() || name == "none")
        {
            juce::MessageManager::callAsync ([this, a = alive] { if (a->load()) clearReference(); });
            return reply ("reference cleared");
        }
        StoredReference r;
        if (! ReferenceStore::load (name, r)) return failure ("no reference named \"" + name + "\"");
        juce::MessageManager::callAsync ([this, r, a = alive] { if (a->load()) applyReference (r); });
        return reply ("reference set to " + name);
    }

    return failure ("unknown command: " + cmd);
}

juce::AudioProcessorEditor* KeecoMixFeedbackProcessor::createEditor()
{
    return new KeecoMixFeedbackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeecoMixFeedbackProcessor();
}
