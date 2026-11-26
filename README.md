# MixSight – prototyp v0.5

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
| Claude | voliteľne aj aplikácia Claude cez MCP – Claude číta merania a píše postrehy do okna pluginu, pozri `CLAUDE-SETUP.md` |
| AI chat | záložka **AI chat** v okne pluginu: píšeš otázky, odpovedá Gemini cez Antigravity CLI, vidí merania a vypočuje si vybrané takty (plugin nahráva prehrávanie, 16 kHz mono), pozri `NAVOD.md` |

Meranie sa začína vždy znova, keď v DAW stlačíš Play – od miesta, odkiaľ púšťaš.
Každých 8 taktov plugin uloží súhrn úseku (JSON); to je podklad pre AI vrstvu v ďalšom kroku.

## Inštalácia
Návod pre používateľov je v `NAVOD.md`: spusti `MixSight-Setup-<verzia>.exe` z
[Releases](https://github.com/miroslavlacek/keeco-mix-feedback/releases/latest). Zmeny sú v `CHANGELOG.md`.

## Vydanie novej verzie
1. Zvýš `VERSION` v `CMakeLists.txt` (`project(MixSight VERSION x.y.z …)`) a dopíš sekciu `## x.y.z` do `CHANGELOG.md`.
2. Commit, potom tag: `git tag vx.y.z && git push --tags`.
3. GitHub Actions zostaví plugin, otestuje ho a vytvorí **Release** s inštalátorom
   (`MixSight-Setup-x.y.z.exe`) a balíkom aktualizácie (`MixSight-update-x.y.z.zip` + `.sha256`).
   Pluginy používateľov ho nájdú a ponúknu aktualizáciu.

## Zostavenie na vlastnom PC
Visual Studio 2022 s *Desktop development with C++*, potom v *Developer PowerShell for VS*:
```
cmake -S . -B build -A x64
cmake --build build --config Release --target MixSight_VST3 MixSightBridge MixSightHelper
```

## Referencia
1. Do názvu napíš napr. `Critical – liquid`, stlač **Naučiť referenciu**.
2. Pusti v projekte referenčný track (cez master, ideálne drop, aspoň 30 s).
3. Stlač **Ukončiť a uložiť**. Referencia sa uloží do `%APPDATA%\MixSight\References\` a dá sa vybrať v zozname.
Porovnanie je zarovnané na vnímanú hlasitosť, takže nevadí, že je referencia hlasnejšia.

## Claude
Nastavenie prepojenia s aplikáciou Claude je v `CLAUDE-SETUP.md`. Bez neho funguje tlačidlo
**Kopírovať súhrny (JSON)** – súhrny úsekov sa dajú vložiť do ľubovoľného chatu.

## Štruktúra
- `Source/dsp/` – meranie bez JUCE (dá sa testovať samostatne): `LoudnessMeter`, `TruePeakMeter`, `BandMeter`, `SpectrumAnalyzer`, `MixAnalyzer` (pravidlá, takty, súhrny)
- `Source/PluginProcessor.*`, `Source/PluginEditor.*` – plugin a jeho okno
- `Source/McpLink.h` – lokálne spojenie pluginu s mostom (127.0.0.1, port 47321+)
- `bridge/BridgeMain.cpp` – `MixSightBridge.exe`, MCP server pre Antigravity CLI (Gemini) aj aplikáciu Claude
- `bridge/HelperMain.cpp` – `MixSightHelper.exe`, nastavenie AI a prihlásenie (volá ho inštalátor)
- `Source/dsp/AudioCapture.h` – nahrávanie aktuálneho prehrávania pre `capture_audio`
- `gemini/GEMINI.md` – pokyny pre Gemini (inštalátor ho dá do priečinka pluginu)
- `Source/Updater.*` – aktualizácie z GitHub Releases
- `Source/AiSetup.*` – nájdenie/pripojenie Antigravity, povolenie nástrojov
- `installer/MixSight.iss` – inštalátor (Inno Setup)
- `Source/AiChat.cpp`, `Source/ProcessRunner.h` – chat v okne pluginu (spúšťa `agy -p` na pozadí)
- `tests/dsp_tests.cpp` – testy meraní (hlasitosť podľa EBU testovacích signálov, true peak, korelácia, takty, referencia)
- Prahy pravidiel sú v `MixAnalyzer::Thresholds` – sú to východiskové hodnoty, dolaď ich podľa ucha.

## Známe obmedzenia prototypu
- Okno beží 30× za sekundu; pri zatvorenom okne sa meria ďalej, len sa nekreslí spektrum.
- Pásma v reálnom čase sú filtrované crossovermi (LR4), spektrum je len na zobrazenie.
- Postreh o pásme mimo referencie sa môže opakovať každý úsek, kým problém trvá.
