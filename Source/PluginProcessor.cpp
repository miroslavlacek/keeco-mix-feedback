#include "PluginProcessor.h"
#include "PluginEditor.h"

KeecoMixFeedbackProcessor::KeecoMixFeedbackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
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
    analyzer.prepare (sampleRate);
    if (reference.profile.valid)
        analyzer.setReference (reference.profile);
    spectrumFifo.reset();
}

void KeecoMixFeedbackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    // Transport from the host: playing state, position in quarter notes, tempo, time signature.
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

    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
    analyzer.process (left, right, n, t);

    // Feed the spectrum display. If the UI is closed the FIFO simply fills up and new audio is dropped.
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

void KeecoMixFeedbackProcessor::applyReference (const StoredReference& r)
{
    reference = r;
    analyzer.setReference (r.profile);
}

void KeecoMixFeedbackProcessor::clearReference()
{
    reference = {};
    analyzer.setReference ({});
}

void KeecoMixFeedbackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("KeecoMixFeedback");
    if (reference.profile.valid)
        state.setProperty ("reference", ReferenceStore::toJson (reference), nullptr);
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
}

juce::AudioProcessorEditor* KeecoMixFeedbackProcessor::createEditor()
{
    return new KeecoMixFeedbackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeecoMixFeedbackProcessor();
}
