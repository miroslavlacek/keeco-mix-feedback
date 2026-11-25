#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MixAnalyzer.h"
#include "ReferenceStore.h"

class KeecoMixFeedbackProcessor : public juce::AudioProcessor
{
public:
    KeecoMixFeedbackProcessor();
    ~KeecoMixFeedbackProcessor() override = default;

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

    // ---- used by the editor (UI thread) ----
    kmf::MixAnalyzer& getAnalyzer() noexcept { return analyzer; }
    double getSampleRateForUi() const noexcept { return currentSampleRate.load(); }

    // Copies the newest audio for the spectrum (lock-free FIFO, UI side).
    int readSpectrumAudio (float* left, float* right, int maxSamples);

    void applyReference (const StoredReference& r);
    void clearReference();
    const StoredReference& getReference() const noexcept { return reference; }
    bool hasReference() const noexcept { return reference.profile.valid; }

    // Section summaries collected this session (for copying / the AI layer).
    juce::StringArray& getSummaries() noexcept { return summaries; }

private:
    kmf::MixAnalyzer analyzer;
    std::atomic<double> currentSampleRate { 48000.0 };

    static constexpr int fifoSize = 1 << 15;
    juce::AbstractFifo spectrumFifo { fifoSize };
    juce::AudioBuffer<float> spectrumBuffer { 2, fifoSize };

    StoredReference reference;
    juce::StringArray summaries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeecoMixFeedbackProcessor)
};
