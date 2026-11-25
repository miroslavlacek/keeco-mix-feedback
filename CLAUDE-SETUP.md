# Prepojenie Keeco Mix Feedback s aplikáciou Claude

Claude v aplikácii pre počítač sa cez malý pomocný program `KeecoMcpBridge.exe` pripojí k pluginu,
ktorý beží vo FL Studiu. Funguje s tvojím bežným prihlásením do Claude, žiadny API kľúč netreba.
Spojenie medzi pluginom a pomocným programom ide len vo vnútri tvojho PC (127.0.0.1).

## Jednorazové nastavenie

1. Vytvor priečinok `C:\KeecoMixFeedback` a skopíruj doň `KeecoMcpBridge.exe`
   (v stiahnutom balíku je v `build\KeecoMcpBridge_artefacts\Release\`).
2. V aplikácii Claude otvor **Settings → Developer → Edit Config**.
   Otvorí sa súbor `claude_desktop_config.json`.
3. Vlož doň toto (ak už súbor niečo obsahuje, pridaj len časť `"keeco-mix-feedback"` do existujúceho `"mcpServers"`):

   ```json
   {
     "mcpServers": {
       "keeco-mix-feedback": {
         "command": "C:\\KeecoMixFeedback\\KeecoMcpBridge.exe"
       }
     }
   }
   ```
4. Ulož súbor a aplikáciu Claude **úplne ukonči** (aj z ikony pri hodinách) a znova spusti.
5. V novom chate by si mal v ponuke nástrojov vidieť **keeco-mix-feedback**.

## Používanie

- Vo FL Studiu maj plugin na mastri a pusti prehrávanie.
- V chate s Claude píš normálne, napr.:
  - „Ako vyzerá drop od taktu 49? Porovnaj ho s breakdownom."
  - „Čo by som mal v mixe riešiť ako prvé?"
  - „Prepni plugin na master a skontroluj true peak."
- Claude si sám vytiahne merania a dôležité postrehy pošle aj do panelu **Postrehy** v plugine (fialové, označené „Claude").

V spodnej časti okna pluginu vidíš stav: *Claude (MCP): pripravené, port 47321* a po prvom dopyte *spojené*.

## Keď to nefunguje

- **Claude hlási, že plugin nebeží:** FL Studio musí byť otvorené a plugin vložený na mastri (stačí jeden).
- **Nástroj sa v Claude nezobrazuje:** skontroluj cestu v konfigurácii (dvojité `\\`) a či si Claude úplne reštartoval.
- **Windows Firewall sa pýta na povolenie:** spojenie je len lokálne, môžeš povoliť len pre súkromné siete.
