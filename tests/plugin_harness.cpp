// Headless end-to-end check of the real plugin code: fake DAW transport, synthetic DnB-like
// audio, reference learning through the UI button, then a screenshot of the editor.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct FakeHost : juce::AudioPlayHead
{
    bool playing = false; double ppq = 0.0, bpm = 174.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 }); return p;
    }
};

struct Synth
{
    double fs = 48000, t = 0; unsigned seed = 7; double lpState = 0;
    double noise() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 8388608.0 - 1.0; }
    // mix = 0: balanced "reference"; mix = 1: muddy low-mids, stereo bass, hot limiter
    void render (juce::AudioBuffer<float>& b, int n, int mix)
    {
        const double pi = juce::MathConstants<double>::pi, beat = 60.0 / 174.0;
        for (int i = 0; i < n; ++i, t += 1.0 / fs)
        {
            const double pb = std::fmod (t, beat) / beat, bar = std::fmod (t, beat * 4) / (beat * 4);
            const double kick = (pb < 0.25 ? std::exp (-pb * 18) : 0.0) * std::sin (2 * pi * (48 + 90 * std::exp (-pb * 30)) * t);
            const double snare = (std::fabs (bar - 0.25) < 0.04 || std::fabs (bar - 0.75) < 0.04) ? noise() * 0.5 : 0.0;
            const double hat = (std::fmod (t, beat / 2) < 0.015) ? noise() * 0.15 : 0.0;
            const double sub = 0.35 * std::sin (2 * pi * 46.25 * t);
            lpState += 0.05 * (noise() - lpState);
            const double pad = (mix ? 0.55 : 0.18) * (std::sin (2 * pi * 311 * t) * 0.6 + lpState);
            const double air = 0.04 * noise();
            const double wide = mix ? 0.25 * std::sin (2 * pi * 70 * t) : 0.0;
            const double gain = mix ? 2.2 : 1.0;
            double m = gain * (0.8 * kick + snare + hat + sub + pad + air);
            double l = m + wide, r = m - wide + 0.08 * (mix ? noise() : 0);
            if (! mix) { l = std::tanh (l * 0.9); r = std::tanh (r * 0.9); }
            b.setSample (0, i, (float) l); b.setSample (1, i, (float) r);
        }
    }
};

static juce::TextButton* findButton (juce::Component& root, const juce::String& text)
{
    for (auto* c : root.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == text) return b;
    return nullptr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    FakeHost host; proc->setPlayHead (&host);
    proc->setPlayConfigDetails (2, 2, 48000, 512);
    proc->prepareToPlay (48000, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditorAndMakeActive());
    ed->setSize (1180, 760);
    juce::MidiBuffer midi; juce::AudioBuffer<float> buf (2, 512); Synth syn;

    auto play = [&] (double seconds, int mix, double startBar)
    {
        host.playing = true; host.ppq = (startBar - 1) * 4.0;
        const int blocksPerTick = 3;                       // ~32 ms of audio per UI tick
        for (int b = 0; b < (int) (seconds * 48000 / 512); ++b)
        {
            syn.render (buf, 512, mix);
            proc->processBlock (buf, midi);
            host.ppq += 512 * host.bpm / 60.0 / 48000.0;
            if (b % blocksPerTick == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        }
    };
    auto stop = [&] { host.playing = false; proc->processBlock (buf, midi); juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };

    // 1. Learn a reference through the UI.
    auto* learn = findButton (*ed, juce::String::fromUTF8 ("Naučiť referenciu"));
    if (learn == nullptr) { std::puts ("learn button not found"); return 1; }
    learn->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    play (14.0, 0, 1);
    learn->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    stop();

    // 2. Play the "mix" from bar 65.
    play (argc > 2 ? std::atof (argv[2]) : 24.0, 1, 65);

    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out (argc > 1 ? argv[1] : "/tmp/kmf_ui.png");
    out.deleteFile();
    juce::FileOutputStream fos (out);
    juce::PNGImageFormat().writeImageToStream (img, fos);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
