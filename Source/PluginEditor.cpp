#include "PluginEditor.h"

namespace
{
namespace col
{
    const juce::Colour bg      { 0xff0f1115 }, header { 0xff151820 }, panel { 0xff171a21 }, tile { 0xff1c2029 };
    const juce::Colour border  { 0xff262b35 }, plot { 0xff12151b }, grid { 0xff232934 };
    const juce::Colour text    { 0xffe8eaed }, muted { 0xff9aa3af }, faint { 0xff7d8794 };
    const juce::Colour accent  { 0xfff5a524 }, ref { 0xff4cc3ff }, good { 0xff5fd068 };
    const juce::Colour warn    { 0xffffb74d }, bad { 0xffff8a65 };
}

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::String fmt (double v, int decimals = 1, bool sign = false)
{
    if (v <= -150.0) return "--";
    auto s = juce::String (v, decimals);
    return (sign && v > 0) ? "+" + s : s;
}

juce::String timeString (double sec)
{
    const int total = (int) sec;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2)
         + "." + juce::String ((int) ((sec - total) * 10));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (col::panel);  g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (col::border); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::muted);  g.setFont (uiFont (11.0f, true));
    g.drawText (title, r.reduced (16, 12).removeFromTop (14), juce::Justification::centredLeft);
}

void drawTile (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const juce::String& value, juce::Colour valueColour)
{
    g.setColour (col::tile); g.fillRoundedRectangle (r.toFloat(), 6.0f);
    auto in = r.reduced (10, 7);
    g.setColour (col::muted); g.setFont (uiFont (11.5f));
    g.drawText (label, in.removeFromTop (15), juce::Justification::centredLeft);
    g.setColour (valueColour); g.setFont (monoFont (15.5f));
    g.drawText (value, in, juce::Justification::centredLeft);
}

void drawBar (juce::Graphics& g, juce::Rectangle<int> r, float fraction, juce::Colour c)
{
    g.setColour (col::tile); g.fillRoundedRectangle (r.toFloat(), 4.0f);
    g.setColour (c); g.fillRoundedRectangle (r.toFloat().withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, fraction)), 4.0f);
}

juce::Colour severityColour (kmf::Severity s)
{
    return s == kmf::Severity::problem ? col::bad : s == kmf::Severity::warning ? col::warn : col::ref;
}
} // namespace

KeecoMixFeedbackEditor::KeecoMixFeedbackEditor (KeecoMixFeedbackProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    fifoL.resize (1 << 15); fifoR.resize (1 << 15);
    spectrum.setSampleRate (proc.getSampleRateForUi());

    auto styleButton = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, col::tile);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a2f3a));
        b.setColour (juce::TextButton::textColourOffId, col::muted);
        b.setColour (juce::TextButton::textColourOnId, col::text);
        addAndMakeVisible (b);
    };

    for (auto* b : { &modeStereo, &modeMid, &modeSide })
    {
        styleButton (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }
    modeStereo.setToggleState (true, juce::dontSendNotification);
    modeStereo.onClick = [this] { spectrum.setMode (kmf::SpectrumAnalyzer::Mode::stereo); };
    modeMid.onClick    = [this] { spectrum.setMode (kmf::SpectrumAnalyzer::Mode::mid); };
    modeSide.onClick   = [this] { spectrum.setMode (kmf::SpectrumAnalyzer::Mode::side); };

    styleButton (learnButton);
    learnButton.onClick = [this] { toggleLearning(); };
    styleButton (copyButton);
    copyButton.onClick = [this] { copySummaries(); };
    styleButton (clearButton);
    clearButton.onClick = [this] { proc.clearLog(); repaint(); };

    modeBox.addItem ("Premaster", 1);
    modeBox.addItem ("Master", 2);
    modeBox.setColour (juce::ComboBox::backgroundColourId, col::tile);
    modeBox.setColour (juce::ComboBox::textColourId, col::text);
    modeBox.setColour (juce::ComboBox::outlineColourId, col::border);
    modeBox.setSelectedId (proc.getMode() == kmf::MasterMode::master ? 2 : 1, juce::dontSendNotification);
    modeBox.onChange = [this] { proc.setMode (modeBox.getSelectedId() == 2 ? kmf::MasterMode::master : kmf::MasterMode::premaster); };
    addAndMakeVisible (modeBox);

    referenceBox.setColour (juce::ComboBox::backgroundColourId, col::tile);
    referenceBox.setColour (juce::ComboBox::textColourId, col::text);
    referenceBox.setColour (juce::ComboBox::outlineColourId, col::border);
    referenceBox.onChange = [this] { onReferenceChosen(); };
    addAndMakeVisible (referenceBox);

    referenceName.setTextToShowWhenEmpty (u8 ("názov novej referencie"), col::faint);
    referenceName.setColour (juce::TextEditor::backgroundColourId, col::tile);
    referenceName.setColour (juce::TextEditor::textColourId, col::text);
    referenceName.setColour (juce::TextEditor::outlineColourId, col::border);
    addAndMakeVisible (referenceName);

    refreshReferenceList();

    setResizable (true, true);
    setResizeLimits (980, 640, 2000, 1400);
    setSize (1180, 760);
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (30);
}

KeecoMixFeedbackEditor::~KeecoMixFeedbackEditor()
{
    stopTimer();
}

// ------------------------------------------------------------------ data

void KeecoMixFeedbackEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.001, 0.5, now - lastTick);
    lastTick = now;

    m = proc.getAnalyzer().getMeters();

    if (m.playing && ! wasPlaying)              // new playback: fresh session in the UI too
    {
        history.clear(); lastHistorySec = -1.0;
        if (! learning) spectrum.reset();
        spectrum.setSampleRate (proc.getSampleRateForUi());
    }
    wasPlaying = m.playing;

    const int got = proc.readSpectrumAudio (fifoL.data(), fifoR.data(), (int) fifoL.size());
    if (got > 0) spectrum.push (fifoL.data(), fifoR.data(), got);
    if (m.playing) spectrum.update (dt, true);

    if (m.playing && m.seconds - lastHistorySec >= 0.1)
    {
        history.push_back ((float) m.shortTerm);
        lastHistorySec = m.seconds;
    }

    // Claude or the host may change the mode; keep the box in sync.
    const int modeId = proc.getMode() == kmf::MasterMode::master ? 2 : 1;
    if (modeBox.getSelectedId() != modeId) modeBox.setSelectedId (modeId, juce::dontSendNotification);

    repaint();
}

void KeecoMixFeedbackEditor::refreshReferenceList()
{
    referenceBox.clear (juce::dontSendNotification);
    referenceBox.addItem (u8 ("Bez referencie"), 1);
    const auto names = ReferenceStore::listNames();
    for (int i = 0; i < names.size(); ++i)
        referenceBox.addItem (names[i], i + 2);

    const auto current = juce::String::fromUTF8 (proc.getReference().profile.name.c_str());
    const int idx = proc.hasReference() ? names.indexOf (current) : -1;
    referenceBox.setSelectedId (idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
}

void KeecoMixFeedbackEditor::onReferenceChosen()
{
    if (referenceBox.getSelectedId() <= 1) { proc.clearReference(); return; }
    StoredReference r;
    if (ReferenceStore::load (referenceBox.getText(), r))
        proc.applyReference (r);
}

void KeecoMixFeedbackEditor::toggleLearning()
{
    auto& an = proc.getAnalyzer();
    if (! learning)
    {
        learning = true;
        spectrum.reset();
        an.startLearning();
        learnButton.setButtonText (u8 ("Ukončiť a uložiť"));
        proc.addNote (u8 ("Učím sa referenciu: pusti referenčný track (aspoň 30 s, ideálne celý drop) a potom stlač Ukončiť."));
        return;
    }

    learning = false;
    learnButton.setButtonText (u8 ("Naučiť referenciu"));
    auto name = referenceName.getText().trim();
    if (name.isEmpty()) name = "Referencia " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");

    StoredReference r;
    r.profile = an.stopLearning (name.toStdString());
    if (! r.profile.valid)
    {
        proc.addNote (u8 ("Referencia sa neuložila: počul som menej ako 10 s hudby."), kmf::Severity::warning);
        return;
    }
    r.spectrumCurve = spectrum.getAverage();
    ReferenceStore::save (r);
    proc.applyReference (r);
    referenceName.clear();
    refreshReferenceList();
    proc.addNote (u8 ("Referencia „") + name + u8 ("“ uložená a zapnutá."));
}

void KeecoMixFeedbackEditor::copySummaries()
{
    juce::SystemClipboard::copyTextToClipboard (proc.getSectionsJson().replace ("},{\"bars\"", "},\n{\"bars\""));
    proc.addNote (u8 ("Skopírovaných ") + juce::String (proc.getSectionCount()) + u8 (" súhrnov úsekov do schránky."));
}

// ------------------------------------------------------------------ layout & paint

void KeecoMixFeedbackEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (52);
    r.reduce (20, 16);

    auto left = r.removeFromLeft (300);
    r.removeFromLeft (16);
    loudArea   = left.removeFromTop (juce::jmin (290, left.getHeight() / 2)); left.removeFromTop (12);
    dynArea    = left.removeFromTop (150);                                    left.removeFromTop (12);
    stereoArea = left.removeFromTop (juce::jmin (150, left.getHeight()));

    auto top = r.removeFromTop ((int) (r.getHeight() * 0.62f));
    r.removeFromTop (12);
    alertsArea = r;

    spectrumArea = top;
    auto spIn = top.reduced (16, 12);
    auto modeRow = spIn.removeFromTop (24).removeFromRight (210);
    modeSide.setBounds (modeRow.removeFromRight (66)); modeRow.removeFromRight (6);
    modeMid.setBounds (modeRow.removeFromRight (66));  modeRow.removeFromRight (6);
    modeStereo.setBounds (modeRow.removeFromRight (66));
    bandsArea = spIn.removeFromBottom (50);

    auto h = headerArea.reduced (20, 11);
    clearButton.setBounds (h.removeFromRight (72)); h.removeFromRight (8);
    learnButton.setBounds (h.removeFromRight (140)); h.removeFromRight (8);
    referenceName.setBounds (h.removeFromRight (136)); h.removeFromRight (8);
    referenceBox.setBounds (h.removeFromRight (170)); h.removeFromRight (8);
    modeBox.setBounds (h.removeFromRight (120));

    auto al = alertsArea.reduced (16, 10).removeFromTop (22);
    copyButton.setBounds (al.removeFromRight (190));
}

void KeecoMixFeedbackEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    paintHeader (g, headerArea);
    paintLoudness (g, loudArea);
    paintDynamics (g, dynArea);
    paintStereo (g, stereoArea);
    paintSpectrum (g, spectrumArea);
    paintBands (g, bandsArea);
    paintAlerts (g, alertsArea);
}

void KeecoMixFeedbackEditor::paintHeader (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (col::header); g.fillRect (r);
    g.setColour (col::border); g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) getWidth());

    auto in = r.reduced (20, 0);
    auto logo = in.removeFromLeft (28).withSizeKeepingCentre (28, 28);
    g.setColour (col::accent); g.fillRoundedRectangle (logo.toFloat(), 6.0f);
    juce::Path wave; const auto lf = logo.toFloat().reduced (6.0f);
    wave.startNewSubPath (lf.getX(), lf.getBottom() - 3); wave.lineTo (lf.getX() + lf.getWidth() * 0.25f, lf.getY() + 5);
    wave.lineTo (lf.getCentreX(), lf.getBottom() - 6); wave.lineTo (lf.getX() + lf.getWidth() * 0.75f, lf.getY() + 1);
    wave.lineTo (lf.getRight(), lf.getCentreY());
    g.setColour (col::bg); g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    in.removeFromLeft (12);

    g.setColour (col::text); g.setFont (uiFont (15.0f, true));
    g.drawText ("Keeco Mix Feedback", in.removeFromLeft (160), juce::Justification::centredLeft);

    // Status pill
    auto pill = in.removeFromLeft (240).withSizeKeepingCentre (240, 28);
    const bool on = m.playing;
    g.setColour (learning ? juce::Colour (0xff12283a) : on ? juce::Colour (0xff1e2a1f) : col::tile);
    g.fillRoundedRectangle (pill.toFloat(), 14.0f);
    g.setColour (learning ? col::ref : on ? col::good : col::faint);
    g.fillEllipse (pill.getX() + 12.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
    juce::String status = learning ? u8 ("UČÍM SA") : on ? "PLAYING" : "STOPPED";
    if (on && m.bar > 0) status += u8 ("  ·  takt ") + juce::String (m.bar);
    if (on) status += u8 ("  ·  ") + timeString (m.seconds);
    g.setFont (monoFont (12.0f));
    g.drawText (status, pill.withTrimmedLeft (28), juce::Justification::centredLeft);
}

void KeecoMixFeedbackEditor::paintLoudness (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, u8 ("HLASITOSŤ"));
    auto in = r.reduced (16, 12); in.removeFromTop (22);

    auto big = in.removeFromTop (38);
    g.setColour (col::muted); g.setFont (uiFont (13.0f));
    g.drawText ("Short-term", big, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (monoFont (30.0f, true));
    g.drawText (fmt (m.shortTerm) + " LUFS", big, juce::Justification::centredRight);
    in.removeFromTop (8);

    auto row1 = in.removeFromTop (48); in.removeFromTop (8);
    auto row2 = in.removeFromTop (48); in.removeFromTop (10);
    const int w = (row1.getWidth() - 8) / 2;
    drawTile (g, row1.removeFromLeft (w), "Momentary", fmt (m.momentary), col::text);  row1.removeFromLeft (8);
    drawTile (g, row1, "Integrated", fmt (m.integrated), col::text);
    const auto tpColour = m.truePeakMaxDb > 0.0 ? col::bad : m.truePeakMaxDb > -1.0 ? col::warn : col::text;
    drawTile (g, row2.removeFromLeft (w), "True peak max", fmt (m.truePeakMaxDb, 1, true) + " dBTP", tpColour); row2.removeFromLeft (8);
    drawTile (g, row2, "LRA", fmt (m.lra) + " LU", col::text);

    // Short-term history since playback start.
    auto plot = in.withHeight (juce::jmax (40, in.getHeight()));
    g.setColour (col::plot); g.fillRoundedRectangle (plot.toFloat(), 4.0f);
    if (history.size() > 1)
    {
        const float lo = -30.0f, hi = 0.0f;
        juce::Path p; bool started = false;
        const size_t n = history.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (history[i] < -100.0f) { started = false; continue; }      // silence / not measured yet
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            const float v = juce::jlimit (lo, hi, history[i]);
            const float y = plot.getBottom() - (v - lo) / (hi - lo) * plot.getHeight();
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (col::accent); g.strokePath (p, juce::PathStrokeType (1.8f));
    }
    g.setColour (col::faint); g.setFont (uiFont (10.0f));
    g.drawText (u8 ("short-term od spustenia (-30…0 LUFS)"), plot.reduced (6, 3), juce::Justification::topLeft);
}

void KeecoMixFeedbackEditor::paintDynamics (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, "DYNAMIKA");
    auto in = r.reduced (16, 12); in.removeFromTop (24);
    g.setFont (uiFont (12.0f));

    auto line = [&] (const juce::String& label, double v, juce::Colour c)
    {
        auto row = in.removeFromTop (18);
        g.setColour (col::muted); g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (monoFont (12.5f));
        g.drawText (m.playing || std::abs (v) > 1e-9 ? juce::String (v, 1) + " dB" : "--", row, juce::Justification::centredRight);
        g.setFont (uiFont (12.0f));
        in.removeFromTop (4);
        drawBar (g, in.removeFromTop (8), (float) (v / 15.0), c);
        in.removeFromTop (10);
    };
    line (u8 ("PLR (celé prehrávanie)"), m.plrDb, col::ref);
    line (u8 ("PSR (posledné 3 s)"), m.psrDb, m.psrDb > 0 && m.psrDb < 6.0 ? col::warn : col::ref);

    g.setColour (col::faint); g.setFont (uiFont (11.0f));
    const auto refText = proc.hasReference()
        ? u8 ("Referencia PSR: ") + juce::String (proc.getReference().profile.psrDb, 1) + " dB"
        : u8 ("Pod 6 dB PSR = silno stlačené");
    g.drawText (refText, in.removeFromTop (16), juce::Justification::centredLeft);
}

void KeecoMixFeedbackEditor::paintStereo (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, "STEREO");
    auto in = r.reduced (16, 12); in.removeFromTop (24);
    g.setFont (uiFont (12.0f));

    auto row = in.removeFromTop (18);
    g.setColour (col::muted); g.drawText (u8 ("Korelácia"), row, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (monoFont (12.5f)); g.drawText (fmt (m.correlation, 2, true), row, juce::Justification::centredRight);
    in.removeFromTop (6);
    auto meter = in.removeFromTop (8);
    g.setColour (col::tile); g.fillRoundedRectangle (meter.toFloat(), 4.0f);
    g.setColour (col::border); g.drawVerticalLine (meter.getCentreX(), meter.getY() - 3.0f, meter.getBottom() + 3.0f);
    const float x = meter.getX() + (float) (m.correlation + 1.0) * 0.5f * meter.getWidth();
    g.setColour (m.correlation < 0 ? col::bad : col::good);
    g.fillRoundedRectangle (x - 2.0f, meter.getY() - 2.0f, 4.0f, 12.0f, 2.0f);
    in.removeFromTop (10);

    g.setFont (uiFont (12.0f));
    row = in.removeFromTop (18);
    g.setColour (col::muted); g.drawText (u8 ("Šírka (podiel side)"), row, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (monoFont (12.5f)); g.drawText (juce::String (juce::roundToInt (m.width * 100)) + " %", row, juce::Justification::centredRight);
    in.removeFromTop (8);

    g.setFont (uiFont (12.0f));
    row = in.removeFromTop (22);
    g.setColour (col::muted); g.drawText ("Bas pod 120 Hz", row, juce::Justification::centredLeft);
    const bool mono = m.bassCorrelation >= 0.7;
    auto badge = row.removeFromRight (96).reduced (0, 2);
    g.setColour (mono ? juce::Colour (0xff1e2a1f) : juce::Colour (0xff3a2a12)); g.fillRoundedRectangle (badge.toFloat(), 4.0f);
    g.setColour (mono ? col::good : col::warn);
    g.drawText (mono ? "mono OK" : u8 ("nie je mono"), badge, juce::Justification::centred);
}

void KeecoMixFeedbackEditor::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, u8 ("SPEKTRUM  ·  1/6 okt.  ·  sklon 4,5 dB/okt."));
    auto in = r.reduced (16, 12); in.removeFromTop (30);
    in.removeFromBottom (58);                      // band tiles
    auto legend = in.removeFromBottom (18); in.removeFromBottom (6);
    auto plot = in;

    g.setColour (col::plot); g.fillRect (plot);
    const double fMin = kmf::SpectrumAnalyzer::minHz, fMax = kmf::SpectrumAnalyzer::maxHz;
    const float dbTop = -10.0f, dbBottom = -100.0f;
    auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * plot.getWidth(); };
    auto yOf = [&] (double db) { return plot.getY() + (float) ((dbTop - juce::jlimit ((double) dbBottom, (double) dbTop, db)) / (dbTop - dbBottom)) * plot.getHeight(); };

    g.setFont (uiFont (10.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (col::grid); g.drawVerticalLine ((int) xOf (f), (float) plot.getY(), (float) plot.getBottom());
        g.setColour (col::faint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (xOf (f) + 3, (float) plot.getBottom() - 14, 40, 12), juce::Justification::left);
    }
    for (float db = -20.0f; db > dbBottom; db -= 20.0f)
        { g.setColour (col::grid); g.drawHorizontalLine ((int) yOf (db), (float) plot.getX(), (float) plot.getRight()); }

    auto curve = [&] (const std::vector<double>& v)
    {
        juce::Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = xOf (kmf::SpectrumAnalyzer::frequencyOf ((int) i)), y = yOf (v[i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    if (proc.hasReference() && proc.getReference().spectrumCurve.size() == (size_t) kmf::SpectrumAnalyzer::numPoints)
    {
        // Loudness-match the reference curve to the current average around 1 kHz…4 kHz.
        const auto& refC = proc.getReference().spectrumCurve; const auto& avg = spectrum.getAverage();
        double offset = 0.0; int cnt = 0;
        for (int i = 0; i < kmf::SpectrumAnalyzer::numPoints; ++i)
        {
            const double f = kmf::SpectrumAnalyzer::frequencyOf (i);
            if (f >= 500 && f <= 4000 && avg[(size_t) i] > -130) { offset += avg[(size_t) i] - refC[(size_t) i]; ++cnt; }
        }
        std::vector<double> shifted (refC);
        if (cnt > 0) for (auto& v : shifted) v += offset / cnt;
        juce::Path rp = curve (shifted), dashed;
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, rp, dashes, 2);
        g.setColour (col::ref); g.fillPath (dashed);
    }

    g.setColour (col::text.withAlpha (0.35f)); g.strokePath (curve (spectrum.getPeak()), juce::PathStrokeType (1.0f));
    g.setColour (col::accent.withAlpha (0.45f)); g.strokePath (curve (spectrum.getLive()), juce::PathStrokeType (1.2f));
    g.setColour (col::accent); g.strokePath (curve (spectrum.getAverage()), juce::PathStrokeType (2.4f));

    g.setFont (uiFont (11.5f)); g.setColour (col::muted);
    auto item = [&] (juce::Colour c, const juce::String& t, bool dashedLine)
    {
        auto a = legend.removeFromLeft (170);
        auto sw = a.removeFromLeft (18).withSizeKeepingCentre (16, 2);
        g.setColour (c);
        if (dashedLine) { g.fillRect (sw.withWidth (6)); g.fillRect (sw.withTrimmedLeft (10)); } else g.fillRect (sw);
        a.removeFromLeft (6); g.setColour (col::muted); g.drawText (t, a, juce::Justification::centredLeft);
    };
    item (col::accent, u8 ("Priemer od spustenia"), false);
    item (col::accent.withAlpha (0.45f), u8 ("Živé"), false);
    item (col::text.withAlpha (0.35f), "Peak hold", false);
    if (proc.hasReference()) item (col::ref, juce::String::fromUTF8 (proc.getReference().profile.name.c_str()), true);
}

void KeecoMixFeedbackEditor::paintBands (juce::Graphics& g, juce::Rectangle<int> r)
{
    const int n = kmf::BandMeter::numBands, gap = 8;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    const bool hasRef = m.hasReference;
    for (int i = 0; i < n; ++i)
    {
        auto tile = r.removeFromLeft (w); r.removeFromLeft (gap);
        const double v = hasRef ? m.deltaDb[(size_t) i] : m.balanceDb[(size_t) i];
        juce::Colour c = col::text;
        if (hasRef) c = std::abs (v) >= 3.0 ? col::warn : std::abs (v) >= 1.5 ? col::text : col::good;
        const juce::String value = ! m.playing && std::abs (v) < 1e-9 ? "--"
                                 : hasRef ? fmt (v, 1, true) + " dB" : fmt (v, 1, true) + " dB rel.";
        drawTile (g, tile, juce::String::fromUTF8 (kmf::BandMeter::bandNames[i]), value, c);
        if (hasRef && std::abs (v) >= 3.0)
            { g.setColour (col::accent); g.drawRoundedRectangle (tile.toFloat().reduced (0.5f), 6.0f, 1.0f); }
    }
}

void KeecoMixFeedbackEditor::paintAlerts (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, "POSTREHY");
    auto in = r.reduced (16, 10);
    auto head = in.removeFromTop (22).withTrimmedRight (200);
    g.setColour (col::faint); g.setFont (uiFont (11.0f));
    const int port = proc.getLinkPort();
    const auto linkText = port > 0 ? u8 ("Claude (MCP): pripravené, port ") + juce::String (port)
                                     + (proc.getLinkRequests() > 0 ? u8 (" · spojené") : juce::String())
                                   : u8 ("Claude (MCP): nedostupné");
    g.drawText (u8 ("úseky po 8 taktov: ") + juce::String (proc.getSectionCount()) + u8 ("  ·  ") + linkText,
                head, juce::Justification::centredRight);
    in.removeFromTop (6);

    const auto items = proc.getLogNewestFirst (40);
    if (items.empty())
    {
        g.setColour (col::faint); g.setFont (uiFont (13.0f));
        g.drawText (m.playing ? u8 ("Zatiaľ bez pripomienok.") : u8 ("Spusti prehrávanie v DAW – meranie začne od miesta spustenia."),
                    in.removeFromTop (20), juce::Justification::centredLeft);
        return;
    }

    const juce::Colour claudeCol { 0xffb388ff };
    for (const auto& a : items)
    {
        if (in.getHeight() < 20) break;
        const bool fromClaude = a.source == KeecoMixFeedbackProcessor::Source::claude;
        juce::AttributedString as;
        if (fromClaude) as.append ("Claude: ", uiFont (13.0f, true), claudeCol);
        as.append (a.text, uiFont (13.0f), col::text);
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        const int badgeW = 64;
        layout.createLayout (as, (float) (in.getWidth() - badgeW - 10));
        const int h = juce::jmax (20, (int) std::ceil (layout.getHeight()));
        auto row = in.removeFromTop (h);
        const auto badgeText = a.bar > 0 ? "t. " + juce::String (a.bar) : a.seconds > 0 ? timeString (a.seconds) : juce::String ("info");
        const auto c = fromClaude ? claudeCol : severityColour (a.severity);
        auto badge = row.removeFromLeft (badgeW).removeFromTop (20);
        g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (c); g.setFont (monoFont (11.0f));
        g.drawText (badgeText, badge, juce::Justification::centred);
        row.removeFromLeft (10);
        layout.draw (g, row.toFloat());
        in.removeFromTop (8);
    }
}
