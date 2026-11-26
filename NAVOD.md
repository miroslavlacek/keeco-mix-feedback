# MixSight – návod

MixSight je plugin na master v DAW (VST3, Windows). Počas prehrávania meria hlasitosť, dynamiku,
stereo a spektrum, upozorní na problémy s číslom taktu a cez **AI chat** sa môžeš pýtať Gemini –
vidí merania a vie si vypočuť takty, ktoré práve hrali.

## Inštalácia

1. Spusti `MixSight-Setup-<verzia>.exe`.
   Windows môže ukázať modré okno „Systém Windows ochránil váš počítač“ – inštalátor nie je digitálne
   podpísaný. Klikni na **Ďalšie informácie → Spustiť aj tak**.
2. Nechaj zaškrtnuté **Pripraviť AI chat** (stiahne Antigravity CLI od Googlu a pripojí ho k pluginu).
3. Na konci nechaj zaškrtnuté **Prihlásiť sa do Gemini teraz**: otvorí sa okno Antigravity – vyber
   **Login with Google** a prihlás sa v prehliadači svojím Google účtom. Potom v okne napíš `/quit`.
4. Vo FL Studiu daj **Options → Manage plugins → Find plugins** (alebo reštartuj FL Studio)
   a vlož **MixSight** na master.

Plugin sa nainštaluje do `C:\Program Files\Common Files\VST3\MixSight\`. V tom istom priečinku sú aj
pomocné programy (most pre AI `MixSightBridge.exe`, `MixSightHelper.exe`), pokyny pre AI `GEMINI.md`
a nahrávky taktov v `captures`.

## Používanie

- Pusti prehrávanie – meranie začína vždy od miesta spustenia.
- **Prejdi myšou** na ľubovoľný údaj – vľavo dole sa zobrazí, čo znamená.
- **Naučiť referenciu:** pusti hotový track, ktorý sa ti páči, a plugin porovná tvoj mix s ním.
- **Premaster / Master:** podľa toho, či mix ešte pôjde na mastering.
- **AI chat:** napíš do políčka žáner, na ktorom pracuješ, a pýtaj sa, napr.
  „Aký je stav mixu?“, „Vypočuj si takty 33–48, udrie drop dosť?“.

## Aktualizácie

Keď je na GitHube nová verzia, v hlavičke pluginu sa objaví **Nová verzia X**. Po kliknutí uvidíš,
čo je nové, a po potvrdení sa aktualizácia stiahne a nainštaluje. Potom **reštartuj FL Studio**.

## Keď niečo nejde

| Čo vidíš | Čo urobiť |
|---|---|
| V AI chate tlačidlo **Nainštalovať Antigravity** | Klikni naň, počkaj na koniec inštalácie a potom **Pripojiť AI k pluginu**. |
| Tlačidlo **Pripojiť AI k pluginu** | Klikni naň. |
| **Prihlásiť sa cez Google** / chyba prihlásenia | Klikni, vyber Login with Google, prihlás sa a okno zavri príkazom `/quit`. |
| Chceš iný Google účet | **Účet Google** → v okne napíš `/logout`, prihlás sa znova, `/quit`. |
| AI dlho neodpovedá | Prvá odpoveď trvá 10–60 s, pri počúvaní dlhšie. Bezplatný účet má dennú kvótu. |
| FL Studio plugin nevidí | Options → Manage plugins → Find plugins. |

Odinštalovanie: Nastavenia Windows → Aplikácie → MixSight → Odinštalovať.
