# Keeco Mix Feedback s Gemini CLI

Gemini CLI je Gemini v príkazovom riadku. Prihlasuje sa Google účtom a cez `KeecoMcpBridge.exe`
sa pripojí k pluginu vo FL Studiu. Na rozdiel od Claude vie Gemini **aj počúvať**: plugin nahráva
prehrávanie a Gemini si vypočuje vybrané takty (nástroj `capture_audio`).

> Keď Gemini počúva úsek, nahrávka tých taktov sa posiela na servery Google. Čísla z merania idú tiež do Gemini.
> Spojenie medzi pluginom a mostom zostáva vo vnútri PC (127.0.0.1).

## 1. Nainštaluj Gemini CLI (raz)

1. Nainštaluj **Node.js LTS** z https://nodejs.org (predvolené voľby).
2. Otvor **PowerShell** a spusti:
   ```
   npm install -g @google/gemini-cli
   ```
3. Spusti `gemini`, vyber **Login with Google** a prihlás sa v prehliadači. Potom napíš `/quit`.

## 2. Priečinok pre Keeco (raz)

Vytvor `C:\KeecoMixFeedback` a daj doň:
- `KeecoMcpBridge.exe` (z balíka zo stránky GitHub Actions),
- `GEMINI.md` (z priečinka `gemini` v balíku) – Gemini si z neho prečíta, ako s tebou a pluginom pracovať.

Nahrávky úsekov sa budú ukladať do `C:\KeecoMixFeedback\captures`.

## 3. Pripoj most ku Gemini (raz)

Otvor (alebo vytvor) súbor `%USERPROFILE%\.gemini\settings.json` – v PowerShelli:
```
notepad $env:USERPROFILE\.gemini\settings.json
```
a vlož (ak súbor už niečo obsahuje, pridaj len časť `"mcpServers"`):
```json
{
  "mcpServers": {
    "keeco-mix-feedback": {
      "command": "C:\\KeecoMixFeedback\\KeecoMcpBridge.exe",
      "trust": true
    }
  }
}
```
`"trust": true` znamená, že sa Gemini nebude pred každým čítaním meraní pýtať o povolenie. Ak chceš potvrdzovať každý krok, tento riadok vynechaj.

## 4. Používanie

1. Vo FL Studiu maj plugin na mastri a pusti prehrávanie od miesta, ktoré chceš riešiť.
2. V PowerShelli:
   ```
   cd C:\KeecoMixFeedback
   gemini
   ```
   (Gemini spúšťaj vždy v tomto priečinku – inak nemôže čítať nahrávky.)
3. Príkaz `/mcp` ukáže, či je `keeco-mix-feedback` pripojený a aké má nástroje.
4. Píš normálne, napr.:
   - „Vypočuj si takty 33–48 a povedz mi, či drop udrie dosť a či groove nie je stereotypný."
   - „Porovnaj breakdown (17–32) s dropom (49–64) – zvukovo aj podľa čísel."
   - „Čo by si v mixe riešil ako prvé? Dôležité veci pošli do pluginu."

## Keď to nefunguje

- **`/mcp` hlási odpojený server:** skontroluj cestu v `settings.json` (dvojité `\\`) a reštartuj `gemini`.
- **Gemini hlási, že plugin nebeží:** FL Studio musí byť otvorené a plugin na mastri.
- **„takty X–Y nezazneli":** nahrávka platí len pre aktuálne prehrávanie – pusti prehrávanie tak, aby tie takty prehrali.
- **Gemini nevie prečítať WAV:** spusti `gemini` v `C:\KeecoMixFeedback` (súbory mimo priečinka nesmie čítať).
