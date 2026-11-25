#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "dsp/SpectrumAnalyzer.h"

class KeecoMixFeedbackEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit KeecoMixFeedbackEditor (KeecoMixFeedbackProcessor&);
    ~KeecoMixFeedbackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void paintHeader (juce::Graphics&, juce::Rectangle<int>);
    void paintLoudness (juce::Graphics&, juce::Rectangle<int>);
    void paintDynamics (juce::Graphics&, juce::Rectangle<int>);
    void paintStereo (juce::Graphics&, juce::Rectangle<int>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void paintBands (juce::Graphics&, juce::Rectangle<int>);
    void paintAlerts (juce::Graphics&, juce::Rectangle<int>);

    void refreshReferenceList();
    void onReferenceChosen();
    void toggleLearning();
    void copySummaries();

    KeecoMixFeedbackProcessor& proc;
    kmf::SpectrumAnalyzer spectrum;
    std::vector<float> fifoL, fifoR;
    double lastTick = 0.0;

    kmf::MixAnalyzer::Meters m {};
    bool wasPlaying = false, learning = false;
    std::vector<float> history;            // short-term LUFS every 100 ms since playback start
    double lastHistorySec = -1.0;


    // layout
    juce::Rectangle<int> headerArea, loudArea, dynArea, stereoArea, spectrumArea, bandsArea, alertsArea;

    juce::TextButton modeStereo { "Stereo" }, modeMid { "Mid" }, modeSide { "Side" };
    juce::ComboBox referenceBox, modeBox;
    juce::TextEditor referenceName;
    juce::TextButton learnButton { juce::String::fromUTF8 ("Naučiť referenciu") };
    juce::TextButton copyButton  { juce::String::fromUTF8 ("Kopírovať súhrny (JSON)") };
    juce::TextButton clearButton { juce::String::fromUTF8 ("Vymazať") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeecoMixFeedbackEditor)
};
