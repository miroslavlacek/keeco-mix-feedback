#include "PluginEditor.h"
#include "AiSetup.h"
#include "AppPaths.h"

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

// ---- beginner explanations (left-bottom panel)
enum HelpKey
{
    hStatus, hShort, hMomentary, hIntegrated, hTruePeak, hLra, hHistory,
    hPlr, hPsr, hCorrelation, hWidth, hBassMono,
    hSpectrum, hLegendAvg, hLegendLive, hLegendPeak, hLegendRef, hSpecMode,
    hBandSub, hBandBass, hBandLowMid, hBandMid, hBandHigh,
    hMode, hReference, hRefName, hLearn, hClear, hTabs, hCopy, hAllow, hNewChat, hChat,
    hAccount, hGenre, hUpdate,
    hNumKeys
};

struct HelpText { const char* title; const char* body; };

const HelpText helpTexts[hNumKeys] =
{
    { "Stav prehrávania", "Či DAW hrá, aktuálny takt a čas. Meranie sa začína vždy znova od miesta, kde spustíš prehrávanie – hodnoty platia len pre toto prehrávanie." },
    { "Short-term (LUFS)", "Ako hlasno track znie za posledné 3 sekundy, tak ako to vníma ucho. Čím bližšie k 0, tým hlasnejšie. Premaster býva v drope okolo -14 až -10, hotový DnB master okolo -8 až -6." },
    { "Momentary", "Hlasitosť za posledných 0,4 sekundy. Skáče s každým úderom – vidíš na ňom jednotlivé údery, na celkové hodnotenie slúži short-term a integrated." },
    { "Integrated", "Priemerná hlasitosť od spustenia (úplné ticho sa nepočíta). Toto číslo porovnávajú streamovacie služby – Spotify či YouTube hlasnejšie tracky stišujú." },
    { "True peak max (dBTP)", "Najvyšší skutočný vrchol signálu od spustenia, aj medzi vzorkami. Nad 0 hrozí skreslenie v MP3 alebo v reproduktoroch. Premaster drž pod -1 dBTP, nech má mastering rezervu." },
    { "LRA (rozsah hlasitosti)", "O koľko sa líšia tiché a hlasné časti tracku – napríklad intro oproti dropu. Malé číslo = track je stále rovnako hlasný, veľké = výrazné rozdiely medzi časťami." },
    { "Graf hlasitosti", "Short-term hlasitosť od spustenia prehrávania. Vidíš, kde track zosilnie (drop) a kde zoslabne (breakdown), a či sú dropy rovnako hlasné." },
    { "PLR", "Rozdiel medzi najvyšším vrcholom a priemernou hlasitosťou celého prehrávania. Viac dB = údery majú priestor (dynamika), málo dB = mix je silno stlačený." },
    { "PSR", "To isté ako PLR, ale za posledné 3 sekundy. Pod ~6 dB znie mix stlačene a údery strácajú punch. V premastri je dobré mať aspoň ~8 dB." },
    { "Korelácia", "Nakoľko sú ľavý a pravý kanál rovnaké. +1 = mono, okolo 0 = veľmi široké, pod 0 = kanály idú proti sebe a v mone (klub, mobil) sa časť zvuku vyruší." },
    { "Šírka", "Koľko energie je v bokoch (rozdiel ľavého a pravého kanála). 0 % = úplne mono, približne 20–50 % je bežné stereo, veľmi vysoké hodnoty sú citlivé na prehrávanie v mone." },
    { "Bas pod 120 Hz", "Či sú najnižšie basy rovnaké v oboch kanáloch. Klubové aparatúry hrajú sub často mono – stereo bas tam môže zoslabnúť alebo sa rozmazať. Má svietiť „mono OK“." },
    { "Spektrum", "Ako je energia rozložená od basov (vľavo) po výšky (vpravo). Krivka je naklonená o 4,5 dB na oktávu, aby vyvážený mix vyzeral približne vodorovne." },
    { "Priemer od spustenia", "Priemerné spektrum celého prehrávania (hrubá oranžová čiara). Podľa neho posudzuj vyváženie basov, stredov a výšok." },
    { "Živé", "Spektrum práve v tejto chvíli. Rýchlo sa mení s hudbou – hodí sa na sledovanie jednotlivých zvukov." },
    { "Peak hold", "Najvyššia úroveň, ktorú každá frekvencia dosiahla od spustenia. Ukáže rezonancie a piskľavé miesta, ktoré v priemere zaniknú." },
    { "Referencia (prerušovaná čiara)", "Spektrum referenčného tracku zrovnané na rovnakú hlasitosť. Kde je tvoja krivka nad ňou, máš tých frekvencií viac ako referencia; kde je pod ňou, menej." },
    { "Stereo / Mid / Side", "Stereo = celý signál. Mid = stred, čo je rovnaké vľavo aj vpravo (kick, bas, vokál). Side = boky, rozdiel kanálov (šírka, reverby, pady)." },
    { "Sub 20–60 Hz", "Najhlbší bas, ktorý v klube skôr cítiš, ako počuješ. Bez referencie je číslo úroveň pásma voči celkovej hlasitosti; s referenciou rozdiel oproti nej (+ = viac)." },
    { "Bas 60–250 Hz", "Telo kicku a basu. Veľa = mix duní, málo = chýba váha. S referenciou ukazuje rozdiel oproti nej (+ = viac ako referencia)." },
    { "Nízke stredy 250–500 Hz", "Oblasť, kde mix najľahšie zahustne („bahno“). Priveľa = zahltené a nejasné, primálo = tenké. S referenciou ukazuje rozdiel oproti nej." },
    { "Stredy 0,5–4 kHz", "Väčšina nástrojov, snare a vokály; tu je ucho najcitlivejšie. Priveľa = tvrdé a unavujúce, primálo = zvuk je vzadu." },
    { "Výšky 4 kHz+", "Hi-haty, vzduch a iskra. Primálo = tupé, priveľa = ostré a syčivé. V intre bez hi-hatov je prirodzene nízko." },
    { "Premaster / Master", "Premaster = mix pôjde ešte na mastering: plugin hlasitosť nehodnotí, sleduje rezervu a dynamiku. Master = hotový track, posudzuje aj hlasitosť." },
    { "Referencia", "Vyber uložený referenčný track. Plugin potom porovnáva pásma a spektrum tvojho mixu s ním – najistejší spôsob, ako posúdiť vyváženie." },
    { "Názov referencie", "Sem napíš názov, pod ktorým sa nová referencia uloží (napr. meno tracku). Ak necháš prázdne, použije sa dátum." },
    { "Naučiť referenciu", "Daj na master hotový track, ktorý sa ti páči, stlač tlačidlo, pusti ho aspoň 30 s (ideálne drop) a stlač „Ukončiť a uložiť“. Plugin si zapamätá jeho vyváženie." },
    { "Vymazať", "Vymaže postrehy a súhrny úsekov v okne pluginu. Merania bežia ďalej." },
    { "Postrehy / AI chat", "Postrehy = upozornenia pluginu s číslom taktu. AI chat = otázky pre Gemini, ktoré vidí merania a vie si vypočuť takty, ktoré práve hrali." },
    { "Kopírovať súhrny", "Skopíruje štatistiky po 8 taktoch (JSON) do schránky, aby si ich mohol vložiť do iného chatu s AI." },
    { "Nastavenie AI", "Ďalší krok, ktorý chýba, aby AI chat fungoval: nainštalovať Antigravity, pripojiť ho k pluginu, povoliť nástroje pluginu alebo sa prihlásiť cez Google." },
    { "Nový rozhovor", "Začne nový rozhovor s AI – zabudne predchádzajúce otázky. Hodí sa pri inom tracku." },
    { "AI chat", "Napíš otázku a stlač Enter. AI dostane aktuálne merania, pri otázkach na zvuk si nahrá a vypočuje takty. Počúvať sa dajú len takty, ktoré v tomto prehrávaní zazneli." },
    { "Účet Google", "Otvorí okno Antigravity na prihlásenie do Gemini cez Google účet. Na zmenu účtu v ňom napíš /logout a prihlás sa znova; okno zavrieš príkazom /quit." },
    { "Žáner", "Napíš, na akom žánri pracuješ (napr. minimal deep DnB, techno, lo-fi). AI podľa toho hodnotí – čo je v jednom žánri chyba, môže byť v inom zámer." },
    { "Aktualizácia", "Na GitHube je novšia verzia MixSight. Po kliknutí uvidíš, čo je nové, a po potvrdení sa stiahne a nainštaluje. Nová verzia sa načíta po reštarte DAW." },
};

juce::Colour severityColour (kmf::Severity s)
{
    return s == kmf::Severity::problem ? col::bad : s == kmf::Severity::warning ? col::warn : col::ref;
}
} // namespace

MixSightEditor::MixSightEditor (MixSightProcessor& p)
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

    // ---- AI chat
    for (auto* b : { &tabInsights, &tabChat })
    {
        styleButton (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
    }
    tabInsights.setToggleState (true, juce::dontSendNotification);
    tabInsights.onClick = [this] { showTab (false); };
    tabChat.onClick     = [this] { showTab (true); };

    chatView.setMultiLine (true, true);
    chatView.setReadOnly (true);
    chatView.setCaretVisible (false);
    chatView.setScrollbarsShown (true);
    chatView.setColour (juce::TextEditor::backgroundColourId, col::plot);
    chatView.setColour (juce::TextEditor::outlineColourId, col::border);
    chatView.setColour (juce::TextEditor::focusedOutlineColourId, col::border);
    chatView.setColour (juce::TextEditor::highlightColourId, col::ref.withAlpha (0.3f));
    addChildComponent (chatView);

    chatInput.setTextToShowWhenEmpty (u8 ("Napíš otázku, napr. „Vypočuj si takty 33–48, udrie drop dosť?“  (Enter = odoslať)"), col::faint);
    chatInput.setColour (juce::TextEditor::backgroundColourId, col::tile);
    chatInput.setColour (juce::TextEditor::textColourId, col::text);
    chatInput.setColour (juce::TextEditor::outlineColourId, col::border);
    chatInput.setFont (uiFont (14.0f));
    chatInput.onReturnKey = [this] { sendChat(); };
    addChildComponent (chatInput);

    styleButton (sendButton);    sendButton.onClick = [this] { if (proc.getAiChat().isBusy()) proc.getAiChat().cancel(); else sendChat(); };
    styleButton (newChatButton); newChatButton.onClick = [this] { proc.getAiChat().newConversation(); refreshChatView (true); };
    styleButton (allowButton);
    allowButton.setColour (juce::TextButton::textColourOffId, col::accent);
    allowButton.onClick = [this] { onSetupButton(); };
    styleButton (accountButton);
    accountButton.onClick = [this]
    {
        juce::String err;
        if (! proc.getAiChat().openSignIn (err))
            proc.getAiChat().refreshSetup();
        refreshChatView (true);
        updateChatControls();
    };

    genreEditor.setTextToShowWhenEmpty (u8 ("žáner (napr. deep DnB)"), col::faint);
    genreEditor.setColour (juce::TextEditor::backgroundColourId, col::tile);
    genreEditor.setColour (juce::TextEditor::textColourId, col::text);
    genreEditor.setColour (juce::TextEditor::outlineColourId, col::border);
    genreEditor.setText (proc.getGenre(), juce::dontSendNotification);
    genreEditor.onTextChange = [this] { proc.setGenre (genreEditor.getText()); };
    addChildComponent (genreEditor);

    for (auto* c : { (juce::Component*) &sendButton, (juce::Component*) &newChatButton,
                     (juce::Component*) &allowButton, (juce::Component*) &accountButton })
        c->setVisible (false);

    // ---- updates
    styleButton (updateButton);
    updateButton.setColour (juce::TextButton::buttonColourId, col::accent.withAlpha (0.18f));
    updateButton.setColour (juce::TextButton::textColourOffId, col::accent);
    updateButton.onClick = [this] { onUpdateButton(); };
    updateButton.setVisible (false);
    proc.getUpdater().checkIfDue();
    const auto updatedNote = proc.getUpdater().getInstalledNote();
    if (updatedNote.isNotEmpty()) proc.addNote (updatedNote);

    setResizable (true, true);
    setResizeLimits (980, 760, 2000, 1400);
    setSize (1180, 880);
    addMouseListener (this, true);          // hover help also over buttons and text fields
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (30);
}

MixSightEditor::~MixSightEditor()
{
    stopTimer();
}

// ------------------------------------------------------------------ data

void MixSightEditor::timerCallback()
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

    if (chatTab)
    {
        if (++setupRefreshTicks >= 60) { setupRefreshTicks = 0; proc.getAiChat().refreshSetup(); }   // every 2 s
        refreshChatView (false);
        updateChatControls();
    }
    updateUpdateButton();

    // The AI or the host may change the mode; keep the box in sync.
    const int modeId = proc.getMode() == kmf::MasterMode::master ? 2 : 1;
    if (modeBox.getSelectedId() != modeId) modeBox.setSelectedId (modeId, juce::dontSendNotification);

    repaint();
}

void MixSightEditor::refreshReferenceList()
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

void MixSightEditor::onReferenceChosen()
{
    if (referenceBox.getSelectedId() <= 1) { proc.clearReference(); return; }
    StoredReference r;
    if (ReferenceStore::load (referenceBox.getText(), r))
        proc.applyReference (r);
}

void MixSightEditor::toggleLearning()
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

void MixSightEditor::copySummaries()
{
    juce::SystemClipboard::copyTextToClipboard (proc.getSectionsJson().replace ("},{\"bars\"", "},\n{\"bars\""));
    proc.addNote (u8 ("Skopírovaných ") + juce::String (proc.getSectionCount()) + u8 (" súhrnov úsekov do schránky."));
}

// ------------------------------------------------------------------ layout & paint

void MixSightEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (52);
    r.reduce (20, 16);

    auto left = r.removeFromLeft (300);
    r.removeFromLeft (16);

    // Left column lines up with the right one: loudness = spectrum height,
    // dynamics starts with the insights/chat panel, help fills down to its bottom.
    const int topH = juce::jmax (260, (int) (r.getHeight() * 0.44f));
    auto top = r.removeFromTop (topH);
    r.removeFromTop (12);
    alertsArea = r;

    loudArea   = left.removeFromTop (topH);  left.removeFromTop (12);
    dynArea    = left.removeFromTop (144);   left.removeFromTop (12);
    stereoArea = left.removeFromTop (138);   left.removeFromTop (12);
    helpArea   = left;

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
    referenceName.setBounds (h.removeFromRight (104)); h.removeFromRight (8);
    referenceBox.setBounds (h.removeFromRight (150)); h.removeFromRight (8);
    modeBox.setBounds (h.removeFromRight (112));     h.removeFromRight (10);
    // logo 28 + 12 + title 128 + status 200 = 368 on the left
    updateButton.setBounds (h.removeFromRight (juce::jmin (160, juce::jmax (0, h.getWidth() - 376))));

    auto alIn = alertsArea.reduced (16, 10);
    auto al = alIn.removeFromTop (22);
    tabInsights.setBounds (al.removeFromLeft (92)); al.removeFromLeft (6);
    tabChat.setBounds (al.removeFromLeft (92));     al.removeFromLeft (12);
    auto chatRow = al;
    copyButton.setBounds (al.removeFromRight (190));
    newChatButton.setBounds (chatRow.removeFromRight (116)); chatRow.removeFromRight (8);
    accountButton.setBounds (chatRow.removeFromRight (104)); chatRow.removeFromRight (8);
    allowButton.setBounds (chatRow.removeFromRight (allowButton.isVisible() ? 200 : 0));
    if (allowButton.isVisible()) chatRow.removeFromRight (8);
    genreEditor.setBounds (chatRow.removeFromLeft (juce::jmin (200, chatRow.getWidth())));

    alIn.removeFromTop (8);
    auto inputRow = alIn.removeFromBottom (30);
    sendButton.setBounds (inputRow.removeFromRight (96)); inputRow.removeFromRight (8);
    chatInput.setBounds (inputRow);
    alIn.removeFromBottom (4);
    chatStatusArea = alIn.removeFromBottom (18);
    alIn.removeFromBottom (4);
    chatView.setBounds (alIn);
}

void MixSightEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    helpZones.clear();
    auto zoneOf = [this] (juce::Component& c, int key) { if (c.isVisible()) addZone (c.getBounds(), key); };
    zoneOf (modeBox, hMode); zoneOf (referenceBox, hReference); zoneOf (referenceName, hRefName);
    zoneOf (learnButton, hLearn); zoneOf (clearButton, hClear);
    zoneOf (modeStereo, hSpecMode); zoneOf (modeMid, hSpecMode); zoneOf (modeSide, hSpecMode);
    zoneOf (tabInsights, hTabs); zoneOf (tabChat, hTabs); zoneOf (copyButton, hCopy);
    zoneOf (allowButton, hAllow); zoneOf (newChatButton, hNewChat);
    zoneOf (accountButton, hAccount); zoneOf (genreEditor, hGenre); zoneOf (updateButton, hUpdate);
    zoneOf (chatView, hChat); zoneOf (chatInput, hChat); zoneOf (sendButton, hChat);

    paintHeader (g, headerArea);
    paintLoudness (g, loudArea);
    paintDynamics (g, dynArea);
    paintStereo (g, stereoArea);
    paintSpectrum (g, spectrumArea);
    paintBands (g, bandsArea);
    if (chatTab) paintChat (g, alertsArea); else paintAlerts (g, alertsArea);
    paintHelp (g, helpArea);
}

void MixSightEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();
    for (auto it = helpZones.rbegin(); it != helpZones.rend(); ++it)
        if (it->area.contains (pos))
        {
            if (hoverHelp != it->key) { hoverHelp = it->key; repaint (helpArea); }
            return;
        }
}

void MixSightEditor::paintHelp (juce::Graphics& g, juce::Rectangle<int> r)
{
    if (r.getHeight() < 48) return;
    drawPanel (g, r, {});
    auto in = r.reduced (14, 12);

    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    if (hoverHelp >= 0 && hoverHelp < hNumKeys)
    {
        as.append (u8 (helpTexts[hoverHelp].title) + "\n", uiFont (13.0f, true), col::accent);
        as.append (u8 (helpTexts[hoverHelp].body), uiFont (12.0f), col::text);
    }
    else
        as.append (u8 ("Prejdi myšou na ľubovoľný údaj, graf alebo tlačidlo – tu sa zobrazí, čo znamená."),
                   uiFont (12.0f), col::muted);
    juce::TextLayout layout;
    layout.createLayout (as, (float) in.getWidth());
    g.saveState();
    g.reduceClipRegion (in);
    layout.draw (g, in.toFloat());
    g.restoreState();
}

void MixSightEditor::paintHeader (juce::Graphics& g, juce::Rectangle<int> r)
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
    auto titleArea = in.removeFromLeft (128);
    g.drawText ("MixSight", titleArea.removeFromLeft (76), juce::Justification::centredLeft);
    g.setColour (col::faint); g.setFont (monoFont (11.0f));
    g.drawText ("v" + mixsight::versionString(), titleArea, juce::Justification::centredLeft);

    // Status pill
    auto pill = in.removeFromLeft (200).withSizeKeepingCentre (200, 28);
    addZone (pill, hStatus);
    const bool on = m.playing;
    g.setColour (learning ? juce::Colour (0xff12283a) : on ? juce::Colour (0xff1e2a1f) : col::tile);
    g.fillRoundedRectangle (pill.toFloat(), 14.0f);
    g.setColour (learning ? col::ref : on ? col::good : col::faint);
    g.fillEllipse (pill.getX() + 12.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
    juce::String status = learning ? u8 ("UČÍM SA") : on ? "PLAYING" : "STOPPED";
    if (on && m.bar > 0) status += u8 ("  ·  takt ") + juce::String (m.bar);
    if (on) status += u8 ("  ·  ") + timeString (m.seconds);
    g.setFont (monoFont (11.0f));
    g.drawText (status, pill.withTrimmedLeft (26), juce::Justification::centredLeft);
}

void MixSightEditor::paintLoudness (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, u8 ("HLASITOSŤ"));
    auto in = r.reduced (16, 12); in.removeFromTop (22);

    auto big = in.removeFromTop (38);
    addZone (big, hShort);
    g.setColour (col::muted); g.setFont (uiFont (13.0f));
    g.drawText ("Short-term", big, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (monoFont (30.0f, true));
    g.drawText (fmt (m.shortTerm) + " LUFS", big, juce::Justification::centredRight);
    in.removeFromTop (8);

    auto row1 = in.removeFromTop (48); in.removeFromTop (8);
    auto row2 = in.removeFromTop (48); in.removeFromTop (10);
    const int w = (row1.getWidth() - 8) / 2;
    addZone (row1.withWidth (w), hMomentary); addZone (row1.withTrimmedLeft (w + 8), hIntegrated);
    addZone (row2.withWidth (w), hTruePeak);  addZone (row2.withTrimmedLeft (w + 8), hLra);
    drawTile (g, row1.removeFromLeft (w), "Momentary", fmt (m.momentary), col::text);  row1.removeFromLeft (8);
    drawTile (g, row1, "Integrated", fmt (m.integrated), col::text);
    const auto tpColour = m.truePeakMaxDb > 0.0 ? col::bad : m.truePeakMaxDb > -1.0 ? col::warn : col::text;
    drawTile (g, row2.removeFromLeft (w), "True peak max", fmt (m.truePeakMaxDb, 1, true) + " dBTP", tpColour); row2.removeFromLeft (8);
    drawTile (g, row2, "LRA", fmt (m.lra) + " LU", col::text);

    // Short-term history since playback start.
    auto plot = in.withHeight (juce::jmax (40, in.getHeight()));
    addZone (plot, hHistory);
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

void MixSightEditor::paintDynamics (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, "DYNAMIKA");
    auto in = r.reduced (16, 12); in.removeFromTop (24);
    g.setFont (uiFont (12.0f));

    auto line = [&] (const juce::String& label, double v, juce::Colour c, int key)
    {
        addZone (in.withHeight (40), key);
        auto row = in.removeFromTop (18);
        g.setColour (col::muted); g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (monoFont (12.5f));
        g.drawText (m.playing || std::abs (v) > 1e-9 ? juce::String (v, 1) + " dB" : "--", row, juce::Justification::centredRight);
        g.setFont (uiFont (12.0f));
        in.removeFromTop (4);
        drawBar (g, in.removeFromTop (8), (float) (v / 15.0), c);
        in.removeFromTop (10);
    };
    line (u8 ("PLR (celé prehrávanie)"), m.plrDb, col::ref, hPlr);
    line (u8 ("PSR (posledné 3 s)"), m.psrDb, m.psrDb > 0 && m.psrDb < 6.0 ? col::warn : col::ref, hPsr);

    g.setColour (col::faint); g.setFont (uiFont (11.0f));
    const auto refText = proc.hasReference()
        ? u8 ("Referencia PSR: ") + juce::String (proc.getReference().profile.psrDb, 1) + " dB"
        : u8 ("Pod 6 dB PSR = silno stlačené");
    g.drawText (refText, in.removeFromTop (16), juce::Justification::centredLeft);
}

void MixSightEditor::paintStereo (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, "STEREO");
    auto in = r.reduced (16, 12); in.removeFromTop (24);
    g.setFont (uiFont (12.0f));

    addZone (in.withHeight (42), hCorrelation);
    addZone (in.withTrimmedTop (42).withHeight (26), hWidth);
    addZone (in.withTrimmedTop (68).withHeight (24), hBassMono);
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

void MixSightEditor::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, u8 ("SPEKTRUM  ·  1/6 okt.  ·  sklon 4,5 dB/okt."));
    auto in = r.reduced (16, 12); in.removeFromTop (30);
    in.removeFromBottom (58);                      // band tiles
    auto legend = in.removeFromBottom (18); in.removeFromBottom (6);
    auto plot = in;

    addZone (plot, hSpectrum);
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
    auto item = [&] (juce::Colour c, const juce::String& t, bool dashedLine, int key)
    {
        auto a = legend.removeFromLeft (170);
        addZone (a, key);
        auto sw = a.removeFromLeft (18).withSizeKeepingCentre (16, 2);
        g.setColour (c);
        if (dashedLine) { g.fillRect (sw.withWidth (6)); g.fillRect (sw.withTrimmedLeft (10)); } else g.fillRect (sw);
        a.removeFromLeft (6); g.setColour (col::muted); g.drawText (t, a, juce::Justification::centredLeft);
    };
    item (col::accent, u8 ("Priemer od spustenia"), false, hLegendAvg);
    item (col::accent.withAlpha (0.45f), u8 ("Živé"), false, hLegendLive);
    item (col::text.withAlpha (0.35f), "Peak hold", false, hLegendPeak);
    if (proc.hasReference()) item (col::ref, juce::String::fromUTF8 (proc.getReference().profile.name.c_str()), true, hLegendRef);
}

void MixSightEditor::paintBands (juce::Graphics& g, juce::Rectangle<int> r)
{
    const int n = kmf::BandMeter::numBands, gap = 8;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    const bool hasRef = m.hasReference;
    for (int i = 0; i < n; ++i)
    {
        auto tile = r.removeFromLeft (w); r.removeFromLeft (gap);
        addZone (tile, hBandSub + i);
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

void MixSightEditor::paintAlerts (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, {});
    auto in = r.reduced (16, 10);
    auto head = in.removeFromTop (22).withTrimmedRight (200).withTrimmedLeft (200);
    g.setColour (col::faint); g.setFont (uiFont (11.0f));
    const int port = proc.getLinkPort();
    const auto linkText = port > 0 ? u8 ("AI (MCP): pripravené, port ") + juce::String (port)
                                     + (proc.getLinkRequests() > 0 ? u8 (" · spojené") : juce::String())
                                   : u8 ("AI (MCP): nedostupné");
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

    const juce::Colour aiCol { 0xffb388ff };
    for (const auto& a : items)
    {
        if (in.getHeight() < 20) break;
        const bool fromAi = a.source == MixSightProcessor::Source::ai;
        juce::AttributedString as;
        if (fromAi) as.append ("AI: ", uiFont (13.0f, true), aiCol);
        as.append (a.text, uiFont (13.0f), col::text);
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        const int badgeW = 64;
        layout.createLayout (as, (float) (in.getWidth() - badgeW - 10));
        const int h = juce::jmax (20, (int) std::ceil (layout.getHeight()));
        auto row = in.removeFromTop (h);
        const auto badgeText = a.bar > 0 ? "t. " + juce::String (a.bar) : a.seconds > 0 ? timeString (a.seconds) : juce::String ("info");
        const auto c = fromAi ? aiCol : severityColour (a.severity);
        auto badge = row.removeFromLeft (badgeW).removeFromTop (20);
        g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (c); g.setFont (monoFont (11.0f));
        g.drawText (badgeText, badge, juce::Justification::centred);
        row.removeFromLeft (10);
        layout.draw (g, row.toFloat());
        in.removeFromTop (8);
    }
}

// ------------------------------------------------------------------ AI chat

void MixSightEditor::showTab (bool chat)
{
    chatTab = chat;
    copyButton.setVisible (! chat);
    for (auto* c : { (juce::Component*) &chatView, (juce::Component*) &chatInput, (juce::Component*) &genreEditor,
                     (juce::Component*) &sendButton, (juce::Component*) &newChatButton, (juce::Component*) &accountButton })
        c->setVisible (chat);
    if (chat)
    {
        proc.getAiChat().refreshSetup();
        refreshChatView (true);
        updateChatControls();
        chatInput.grabKeyboardFocus();
    }
    else
        allowButton.setVisible (false);
    resized();
    repaint();
}

void MixSightEditor::sendChat()
{
    auto& chat = proc.getAiChat();
    const auto text = chatInput.getText().trim();
    if (text.isEmpty() || chat.isBusy()) return;
    chat.send (text, proc.buildAiContext(), proc.getGenre());
    chatInput.setText ({}, juce::dontSendNotification);
    chatInput.repaint();
    refreshChatView (true);
    updateChatControls();
}

void MixSightEditor::updateChatControls()
{
    auto& chat = proc.getAiChat();
    const bool busy = chat.isBusy();
    sendButton.setButtonText (busy ? u8 ("Zastaviť") : u8 ("Odoslať"));
    juce::String text;
    switch (chat.getSetupStep())
    {
        case AiChat::SetupStep::installAgy: text = u8 ("Nainštalovať Antigravity"); break;
        case AiChat::SetupStep::connect:    text = u8 ("Pripojiť AI k pluginu"); break;
        case AiChat::SetupStep::allowTools: text = u8 ("Povoliť nástroje pluginu"); break;
        case AiChat::SetupStep::signIn:     text = u8 ("Prihlásiť sa cez Google"); break;
        case AiChat::SetupStep::ready:      break;
    }
    const bool show = chatTab && text.isNotEmpty();
    if (show) allowButton.setButtonText (text);
    accountButton.setEnabled (chat.getAgyExecutable().existsAsFile());
    if (allowButton.isVisible() != show) { allowButton.setVisible (show); resized(); }
}

void MixSightEditor::onSetupButton()
{
    auto& chat = proc.getAiChat();
    juce::String msg;
    switch (chat.getSetupStep())
    {
        case AiChat::SetupStep::installAgy:
        {
            juce::String err;
            msg = aisetup::openAgyInstaller (err)
                ? u8 ("Otvoril som okno s inštaláciou Antigravity. Po jej dokončení okno zavri a klikni na „Pripojiť AI k pluginu“.")
                : u8 ("Inštaláciu sa nepodarilo spustiť (") + err + u8 ("). Nainštaluj Antigravity ručne: irm https://antigravity.google/cli/install.ps1 | iex");
            break;
        }
        case AiChat::SetupStep::connect:
        {
            juce::String log;
            msg = chat.connectBridge (log) ? u8 ("AI je pripojená k pluginu a nástroje pluginu sú povolené. Ak si sa ešte neprihlásil, klikni na „Účet Google“.")
                                           : u8 ("Pripojenie sa nepodarilo: ") + log;
            break;
        }
        case AiChat::SetupStep::allowTools:
        {
            juce::String err;
            msg = chat.allowTools (err) ? u8 ("Nástroje pluginu sú povolené – Antigravity sa už nebude pýtať na potvrdenie.")
                                        : u8 ("Povolenie sa nepodarilo: ") + err;
            break;
        }
        case AiChat::SetupStep::signIn:
        {
            juce::String err;
            if (! chat.openSignIn (err)) msg = u8 ("Okno Antigravity sa nepodarilo otvoriť: ") + err;
            break;
        }
        case AiChat::SetupStep::ready: break;
    }
    if (msg.isNotEmpty()) proc.addNote (msg);
    chat.refreshSetup();
    refreshChatView (true);
    updateChatControls();
}

// ------------------------------------------------------------------ updates

void MixSightEditor::updateUpdateButton()
{
    auto& up = proc.getUpdater();
    juce::String text;
    switch (up.getState())
    {
        case Updater::State::available:   text = u8 ("Nová verzia ") + up.getRelease().version; break;
        case Updater::State::downloading: text = u8 ("Sťahujem ") + juce::String (juce::roundToInt (up.getProgress() * 100)) + " %"; break;
        case Updater::State::installing:  text = u8 ("Inštalujem…"); break;
        case Updater::State::installed:   text = u8 ("Reštartuj DAW"); break;
        case Updater::State::failed:      text = u8 ("Aktualizácia zlyhala"); break;
        default: break;
    }
    const bool show = text.isNotEmpty();
    if (up.getState() == Updater::State::available && ! updateNoted)
    {
        updateNoted = true;
        proc.addNote (u8 ("Je dostupná nová verzia MixSight ") + up.getRelease().version
                      + u8 (" – klikni na „Nová verzia“ v hlavičke."));
    }
    if (show && updateButton.getButtonText() != text) updateButton.setButtonText (text);
    if (updateButton.isVisible() != show) updateButton.setVisible (show);
}

void MixSightEditor::onUpdateButton()
{
    auto& up = proc.getUpdater();
    const auto r = up.getRelease();
    juce::Component::SafePointer<MixSightEditor> safe (this);

    if (up.getState() == Updater::State::available)
    {
        auto notes = AiChat::cleanMarkdown (r.notes);
        if (notes.length() > 900) notes = notes.substring (0, 900) + u8 ("…");
        auto options = juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::InfoIcon)
            .withTitle (u8 ("MixSight ") + r.version + u8 (" je dostupný"))
            .withMessage (u8 ("Máš verziu ") + mixsight::versionString() + u8 (".\n\nČo je nové:\n") + (notes.isNotEmpty() ? notes : u8 ("(bez popisu)"))
                          + u8 ("\n\nAktualizácia sa stiahne z GitHubu a nainštaluje. Nová verzia sa načíta po reštarte DAW."))
            .withButton (u8 ("Stiahnuť a nainštalovať"))
            .withButton (u8 ("Neskôr"))
            .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (options, [safe] (int result)
        {
            if (safe != nullptr && result == 1) safe->proc.getUpdater().downloadAndInstall();
        });
        return;
    }

    juce::String title, message;
    if (up.getState() == Updater::State::installed)
    {
        title = u8 ("Aktualizácia je nainštalovaná");
        message = u8 ("MixSight ") + r.version + u8 (" je nainštalovaný. Ulož projekt a reštartuj DAW (FL Studio) – "
                  "až potom sa načíta nová verzia. Vybratie a opätovné vloženie pluginu nestačí.");
    }
    else if (up.getState() == Updater::State::failed)
    {
        title = u8 ("Aktualizácia zlyhala");
        message = up.getError() + u8 ("\n\nSkús to neskôr znova, alebo si stiahni inštalátor z ") + r.pageUrl;
    }
    else return;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

void MixSightEditor::refreshChatView (bool force)
{
    auto& chat = proc.getAiChat();
    const int v = chat.getVersion();
    if (! force && v == chatVersionShown) return;
    chatVersionShown = v;

    const auto msgs = chat.getMessages();
    const juce::Colour aiCol { 0xffb388ff };
    chatView.clear();
    chatView.setCaretPosition (0);

    auto put = [this] (const juce::String& t, juce::Font f, juce::Colour c)
    {
        chatView.setFont (f);
        chatView.setColour (juce::TextEditor::textColourId, c);
        chatView.insertTextAtCaret (t);
    };

    if (msgs.empty())
    {
        const auto problem = chat.getSetupProblem();
        if (problem.isNotEmpty())
            put (problem + "\n", uiFont (13.5f), col::warn);
        else
            put (u8 ("Píš otázky k mixu, odpovedá Gemini cez Antigravity. Vidí merania z pluginu a na požiadanie si "
                     "vypočuje takty, ktoré práve hrali.\nPríklady:\n  • Aký je stav mixu?\n  • Vypočuj si takty 33–48, udrie drop dosť?\n"
                     "  • Ktorý nástroj v intre môže robiť zápornú koreláciu?\n\nPracovný priečinok: ")
                 + chat.getWorkspace().getFullPathName() + "\n", uiFont (13.5f), col::muted);
    }

    using Role = AiChat::Message::Role;
    for (const auto& msg : msgs)
    {
        switch (msg.role)
        {
            case Role::user:  put (u8 ("Ty:  "), uiFont (13.5f, true), col::accent); put (msg.text + "\n\n", uiFont (13.5f), col::text); break;
            case Role::ai:    put ("AI:  ", uiFont (13.5f, true), aiCol);          put (msg.text + "\n\n", uiFont (13.5f), col::text); break;
            case Role::tool:  put (u8 ("   ↳ ") + msg.text + "\n", monoFont (11.5f), col::faint); break;
            case Role::error: put (msg.text + "\n\n", uiFont (13.0f), col::bad); break;
            case Role::info:  put (msg.text + "\n\n", uiFont (13.0f), col::muted); break;
        }
    }
    chatView.moveCaretToEnd();
    chatView.scrollEditorToPositionCaret (0, chatView.getHeight() - 20);
}

void MixSightEditor::paintChat (juce::Graphics& g, juce::Rectangle<int> r)
{
    drawPanel (g, r, {});
    auto& chat = proc.getAiChat();
    g.setFont (uiFont (11.5f));
    auto st = chat.getStatus();
    if (st.isNotEmpty())
    {
        const int dots = (int) (juce::Time::getMillisecondCounter() / 400) % 4;
        g.setColour (juce::Colour (0xffb388ff));
        g.drawText ("AI: " + st.trimCharactersAtEnd (u8 ("…")) + juce::String::repeatedString (".", dots),
                    chatStatusArea, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (col::faint);
        const int port = proc.getLinkPort();
        g.drawText ((port > 0 ? u8 ("Plugin čaká na AI na porte ") + juce::String (port) : u8 ("Spojenie pluginu nie je dostupné"))
                    + (chat.getConversationId().isNotEmpty() ? u8 ("  ·  rozhovor pokračuje") : juce::String()),
                    chatStatusArea, juce::Justification::centredLeft);
    }
}
