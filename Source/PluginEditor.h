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
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void paintHeader (juce::Graphics&, juce::Rectangle<int>);
    void paintLoudness (juce::Graphics&, juce::Rectangle<int>);
    void paintDynamics (juce::Graphics&, juce::Rectangle<int>);
    void paintStereo (juce::Graphics&, juce::Rectangle<int>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void paintBands (juce::Graphics&, juce::Rectangle<int>);
    void paintAlerts (juce::Graphics&, juce::Rectangle<int>);
    void paintChat (juce::Graphics&, juce::Rectangle<int>);
    void paintHelp (juce::Graphics&, juce::Rectangle<int>);
    void addZone (juce::Rectangle<int> r, int key) { helpZones.push_back ({ r, key }); }
    void showTab (bool chat);
    void sendChat();
    void refreshChatView (bool force);
    void updateChatControls();

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

    // Beginner explanations shown in the left-bottom panel for whatever is under the mouse.
    struct HelpZone { juce::Rectangle<int> area; int key; };
    std::vector<HelpZone> helpZones;          // rebuilt on every paint
    int hoverHelp = -1;
    juce::Rectangle<int> helpArea;

    // AI chat (Antigravity)
    bool chatTab = false;
    int chatVersionShown = -1;
    juce::Rectangle<int> chatStatusArea;
    juce::TextButton tabInsights { "Postrehy" }, tabChat { "AI chat" };
    juce::TextEditor chatView, chatInput;
    juce::TextButton sendButton { juce::String::fromUTF8 ("Odoslať") };
    juce::TextButton newChatButton { juce::String::fromUTF8 ("Nový rozhovor") };
    juce::TextButton allowButton { juce::String::fromUTF8 ("Povoliť nástroje pluginu") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeecoMixFeedbackEditor)
};
