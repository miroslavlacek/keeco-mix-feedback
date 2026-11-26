# Keeco Mix Feedback + Gemini (Antigravity CLI)

Gemini CLI s osobným Google účtom od júna 2026 nefunguje – náhradou je **Antigravity CLI** (`agy`).
Ten istý most `KeecoMcpBridge.exe` funguje aj s ním a od verzie 0.4 sa dá s Gemini písať
**priamo v okne pluginu** (záložka **AI chat**).

## 1. Priečinok a súbory

1. Vytvor priečinok `C:\KeecoMixFeedback`.
2. Skopíruj doň z balíka `KeecoMcpBridge.exe` a `gemini\GEMINI.md`.
3. VST3 z balíka (`VST3\Keeco Mix Feedback.vst3`) skopíruj do `C:\Program Files\Common Files\VST3\`
   (prepíš starú verziu; FL Studio musí byť zatvorené).

## 2. Antigravity CLI

V PowerShelli:

```
irm https://antigravity.google/cli/install.ps1 | iex
```

Zatvor a znova otvor PowerShell. Ak `agy --version` hlási, že príkaz nepozná:

```
[Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path","User") + ";$env:LOCALAPPDATA\agy\bin", "User")
```

## 3. Pridanie mostu a prihlásenie

```
agy mcp add keeco-mix-feedback C:\KeecoMixFeedback\KeecoMcpBridge.exe
cd C:\KeecoMixFeedback
agy
```

Pri prvom spustení vyber **Login with Google**. `/mcp` ukáže server a jeho nástroje. Potom `/quit`.

## 4. Povolenie nástrojov (aby sa nepýtal stále dookola)

Antigravity sa štandardne pýta na každé volanie nástroja z MCP servera. Stačí raz:

- v pluginu v záložke **AI chat** klikni na **Povoliť nástroje pluginu**, alebo
- do `%USERPROFILE%\.gemini\antigravity-cli\settings.json` pridaj:

```json
{
  "permissions": {
    "allow": ["mcp(keeco-mix-feedback/*)"]
  }
}
```

Povolí to len nástroje tohto pluginu (stav, sekcie, nahrávka taktov, postrehy). Čítanie WAV súboru
v pracovnom priečinku Antigravity povolenie nepotrebuje.

## 5. Používanie

**V okne pluginu:** záložka **AI chat**, napíš otázku a Enter. Plugin na pozadí spustí Antigravity
v priečinku `C:\KeecoMixFeedback` (zistí ho z nastavenia mostu), Gemini si prečíta merania, podľa
potreby si vypočuje takty a odpovie. Rozhovor pokračuje, kým nedáš **Nový rozhovor**.
Odpoveď trvá zvyčajne 10–60 s (pri počúvaní dlhšie). **Zastaviť** odpoveď preruší.

**V PowerShelli:** `cd C:\KeecoMixFeedback` a `agy` – funguje rovnako ako predtým.

Príklady otázok:
- Aký je stav mixu?
- Vypočuj si takty 33–48, udrie drop dosť?
- Ktorý nástroj v intre môže robiť zápornú koreláciu?

Počúvať sa dajú len takty, ktoré v aktuálnom prehrávaní naozaj zazneli (plugin drží posledných 10 minút).

## Keď niečo nejde

| Hlásenie v chate | Čo urobiť |
|---|---|
| Nenašiel som Antigravity CLI | Krok 2 (inštalácia). |
| Most KeecoMcpBridge nie je v Antigravity pridaný | Krok 3 (`agy mcp add …`). |
| authentication required / prihlás sa | Spusti `agy` v PowerShelli a prihlás sa. |
| Antigravity odmietol niektorý nástroj | Krok 4 (Povoliť nástroje pluginu). |
| Odpoveď neprichádza vôbec | Skús `agy -p "ahoj"` v PowerShelli – ak ani tam neodpovie, problém je v Antigravity (kvóta, sieť). |

Bezplatný účet má dennú kvótu (Starter Quota); keď sa minie, Antigravity odpovie chybou do ďalšieho dňa.
