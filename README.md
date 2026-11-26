# Keeco Mix Feedback – prototyp v0.4

VST3 plugin na master, ktorý počas prehrávania meria mix a hlási, na čo sa zamerať.
Zvuk len počúva, nič na ňom nemení.

## Čo meria

| Časť | Čo ukazuje |
|---|---|
| Hlasitosť | LUFS momentary (400 ms), short-term (3 s), integrated od spustenia, LRA, max true peak (4× oversampling) – podľa ITU-R BS.1770-4 / EBU R128 |
| Dynamika | PLR (true peak max − integrated) a PSR (true peak − short-term za posledné 3 s) |
| Stereo | korelácia, šírka (podiel side), kontrola, či je bas pod 120 Hz mono |
| Spektrum | FFT 8192, vyhladenie 1/6 oktávy, sklon 4,5 dB/okt., krivky: priemer od spustenia, živé, peak hold, referencia; prepínač Stereo/Mid/Side |
| Pásma | Sub 20–60, Bas 60–250, Nízke stredy 250–500, Stredy 0,5–4k, Výšky 4k+ – úroveň voči vnímanej hlasitosti; s referenciou rozdiel v dB |
| Postrehy | upozornenia s číslom taktu: clipping / true peak nad -1 dBTP, priveľa kompresie (PSR pod 8 dB v režime Premaster, pod 6 dB v režime Master, dlhšie ako 4 s), bas nie je mono, záporná korelácia, pásmo mimo referencie o 3 dB a viac počas 8-taktového úseku, výrazný pokles šírky (napr. mono drop po širokom breakdowne) |
| Režim | Premaster (ide na mastering – stráži rezervu a kompresiu) / Master (hotový master) |
| Claude | prepojenie s aplikáciou Claude cez MCP – Claude číta merania a píše postrehy do okna pluginu, pozri `CLAUDE-SETUP.md` |
| AI chat | záložka **AI chat** v okne pluginu: píšeš otázky, odpovedá Gemini cez Antigravity CLI, vidí merania a vypočuje si vybrané takty (plugin nahráva prehrávanie, 16 kHz mono), pozri `ANTIGRAVITY-SETUP.md` |

Meranie sa začína vždy znova, keď v DAW stlačíš Play – od miesta, odkiaľ púšťaš.
Každých 8 taktov plugin uloží súhrn úseku (JSON); to je podklad pre AI vrstvu v ďalšom kroku.

## Ako dostať plugin na Windows

### Cesta A – bez inštalácie čohokoľvek (GitHub)
1. Založ si na GitHube nový (pokojne súkromný) repozitár a nahraj doň obsah tohto priečinka.
2. Otvor záložku **Actions** → workflow **Build Windows VST3** sa spustí sám (alebo ho spusti tlačidlom *Run workflow*).
3. Po cca 10–15 minútach stiahni v časti **Artifacts** súbor `KeecoMixFeedback-Windows`.

### Cesta B – na vlastnom PC
1. Nainštaluj **Visual Studio 2022 Community** (alebo novšie) s časťou *Desktop development with C++* a **CMake** (je súčasťou VS).
2. V priečinku projektu otvor *Developer PowerShell for VS* a spusti:
   ```
   cmake -S . -B build -A x64
   cmake --build build --config Release --target KeecoMixFeedback_VST3
   ```
   Prvé spustenie si stiahne framework JUCE (potrebuje internet a git).
3. Hotový plugin: `build\KeecoMixFeedback_artefacts\Release\VST3\Keeco Mix Feedback.vst3`

### Inštalácia do FL Studia
1. Skopíruj `Keeco Mix Feedback.vst3` do `C:\Program Files\Common Files\VST3\`.
2. FL Studio → *Options → Manage plugins → Find installed plugins*.
3. Vlož plugin na **Master ako posledný** (za limiter), aby meral to, čo ide von.

## Referencia
1. Do názvu napíš napr. `Critical – liquid`, stlač **Naučiť referenciu**.
2. Pusti v projekte referenčný track (cez master, ideálne drop, aspoň 30 s).
3. Stlač **Ukončiť a uložiť**. Referencia sa uloží do `%APPDATA%\Keeco Mix Feedback\References\` a dá sa vybrať v zozname.
Porovnanie je zarovnané na vnímanú hlasitosť, takže nevadí, že je referencia hlasnejšia.

## Claude
Nastavenie prepojenia s aplikáciou Claude je v `CLAUDE-SETUP.md`. Bez neho funguje tlačidlo
**Kopírovať súhrny (JSON)** – súhrny úsekov sa dajú vložiť do ľubovoľného chatu.

## Štruktúra
- `Source/dsp/` – meranie bez JUCE (dá sa testovať samostatne): `LoudnessMeter`, `TruePeakMeter`, `BandMeter`, `SpectrumAnalyzer`, `MixAnalyzer` (pravidlá, takty, súhrny)
- `Source/PluginProcessor.*`, `Source/PluginEditor.*` – plugin a jeho okno
- `Source/McpLink.h` – lokálne spojenie pluginu s mostom (127.0.0.1, port 47321+)
- `bridge/BridgeMain.cpp` – `KeecoMcpBridge.exe`, MCP server pre Antigravity CLI (Gemini) aj aplikáciu Claude
- `Source/dsp/AudioCapture.h` – nahrávanie aktuálneho prehrávania pre `capture_audio`
- `gemini/GEMINI.md` – pokyny pre Gemini (patrí do `C:\KeecoMixFeedback`)
- `Source/AiChat.cpp`, `Source/ProcessRunner.h` – chat v okne pluginu (spúšťa `agy -p` na pozadí)
- `tests/dsp_tests.cpp` – testy meraní (hlasitosť podľa EBU testovacích signálov, true peak, korelácia, takty, referencia)
- Prahy pravidiel sú v `MixAnalyzer::Thresholds` – sú to východiskové hodnoty, dolaď ich podľa ucha.

## Známe obmedzenia prototypu
- Okno beží 30× za sekundu; pri zatvorenom okne sa meria ďalej, len sa nekreslí spektrum.
- Pásma v reálnom čase sú filtrované crossovermi (LR4), spektrum je len na zobrazenie.
- Postreh o pásme mimo referencie sa môže opakovať každý úsek, kým problém trvá.
