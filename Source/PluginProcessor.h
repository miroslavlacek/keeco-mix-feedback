#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MixAnalyzer.h"
#include "dsp/AudioCapture.h"
#include "ReferenceStore.h"
#include "McpLink.h"
#include "AiChat.h"

class KeecoMixFeedbackProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    KeecoMixFeedbackProcessor();
    ~KeecoMixFeedbackProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    kmf::MixAnalyzer& getAnalyzer() noexcept { return analyzer; }
    double getSampleRateForUi() const noexcept { return currentSampleRate.load(); }
    int readSpectrumAudio (float* left, float* right, int maxSamples);

    void applyReference (const StoredReference& r);
    void clearReference();
    const StoredReference& getReference() const noexcept { return reference; }
    bool hasReference() const noexcept { return reference.profile.valid; }

    void setMode (kmf::MasterMode m) { analyzer.setMode (m); }
    kmf::MasterMode getMode() const noexcept { return analyzer.getMode(); }

    // Log shown in the "Postrehy" panel: plugin rules, notes and the AI's insights.
    enum class Source { rules, plugin, ai };
    struct LogItem { kmf::Severity severity; int bar; double seconds; juce::String text; Source source; };
    std::vector<LogItem> getLogNewestFirst (size_t maxItems) const;
    void addNote (const juce::String& text, kmf::Severity s = kmf::Severity::info);
    void clearLog();

    int getSectionCount() const;
    juce::String getSectionsJson (int fromBar = 0, int toBar = 1 << 30) const;

    int getLinkPort() const noexcept { return link != nullptr ? link->getPort() : 0; }
    int getLinkRequests() const noexcept { return link != nullptr ? link->getRequestCount() : 0; }

    AiChat& getAiChat() noexcept { return *aiChat; }
    juce::String buildAiContext() const;         // current measurements sent along with each chat message

private:
    void timerCallback() override;               // drains the analyzer queues (message thread)
    juce::String handleLinkRequest (const juce::String& json);   // MCP bridge (link thread)
    juce::String statusJson() const;
    void addLog (LogItem item);

    kmf::MixAnalyzer analyzer;
    kmf::AudioCapture capture;                   // current playback, 16 kHz mono, for listening models
    bool captureWasPlaying = false;              // audio thread only
    std::atomic<double> currentSampleRate { 48000.0 }, hostBpm { 0.0 };
    std::atomic<int> hostSigNum { 4 }, hostSigDen { 4 };
    std::atomic<bool> hostHasPosition { false };
    bool wasPlaying = false;

    static constexpr int fifoSize = 1 << 15;
    juce::AbstractFifo spectrumFifo { fifoSize };
    juce::AudioBuffer<float> spectrumBuffer { 2, fifoSize };

    StoredReference reference;
    juce::String referenceName;                  // copy readable from the link thread
    juce::CriticalSection refLock;

    struct SectionItem { int fromBar, toBar; juce::String json; };
    mutable juce::CriticalSection logLock;
    std::vector<LogItem> log;
    std::vector<SectionItem> sections;

    std::unique_ptr<McpLinkServer> link;
    std::unique_ptr<AiChat> aiChat;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeecoMixFeedbackProcessor)
};
