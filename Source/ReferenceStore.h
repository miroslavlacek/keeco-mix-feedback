#pragma once
#include <juce_core/juce_core.h>
#include "dsp/MixAnalyzer.h"
#include "AppPaths.h"

// Saves and loads reference profiles as JSON files in
//   Windows: %APPDATA%\MixSight\References
//   macOS:   ~/Library/Application Support/MixSight/References
struct StoredReference
{
    kmf::ReferenceProfile profile;
    std::vector<double> spectrumCurve;   // average spectrum (dB, 240 points) for the display
};

class ReferenceStore
{
public:
    static juce::File folder()
    {
        auto f = mixsight::appData().getChildFile ("References");
        f.createDirectory();
        return f;
    }

    static juce::StringArray listNames()
    {
        juce::StringArray names;
        for (const auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.json"))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

    static juce::String toJson (const StoredReference& r)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("name", juce::String (r.profile.name));
        juce::Array<juce::var> bal, curve;
        for (double d : r.profile.balanceDb) bal.add (d);
        for (double d : r.spectrumCurve) curve.add (juce::roundToInt (d * 10.0) / 10.0);
        obj->setProperty ("balanceDb", bal);
        obj->setProperty ("shortTermLufs", r.profile.shortTermLufs);
        obj->setProperty ("psrDb", r.profile.psrDb);
        obj->setProperty ("curve", curve);
        return juce::JSON::toString (juce::var (obj.release()), true);
    }

    static bool fromJson (const juce::String& json, StoredReference& out)
    {
        const auto v = juce::JSON::parse (json);
        const auto* bal = v["balanceDb"].getArray();
        if (bal == nullptr || bal->size() != kmf::BandMeter::numBands)
            return false;
        out.profile.name = v["name"].toString().toStdString();
        for (int i = 0; i < kmf::BandMeter::numBands; ++i) out.profile.balanceDb[(size_t) i] = (double) (*bal)[i];
        out.profile.shortTermLufs = (double) v["shortTermLufs"];
        out.profile.psrDb = (double) v["psrDb"];
        out.profile.valid = true;
        out.spectrumCurve.clear();
        if (const auto* c = v["curve"].getArray())
            for (const auto& x : *c) out.spectrumCurve.push_back ((double) x);
        return true;
    }

    static bool save (const StoredReference& r)
    {
        const auto safe = juce::File::createLegalFileName (juce::String (r.profile.name));
        return folder().getChildFile (safe + ".json").replaceWithText (toJson (r));
    }

    static bool load (const juce::String& name, StoredReference& out)
    {
        const auto f = folder().getChildFile (juce::File::createLegalFileName (name) + ".json");
        return f.existsAsFile() && fromJson (f.loadFileAsString(), out);
    }
};
