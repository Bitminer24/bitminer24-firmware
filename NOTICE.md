# Herkunft und Lizenzen

Diese Firmware ist ein Neubau auf ESP-IDF 5.5, enthält aber Bestandteile
aus dem NerdMiner-Projekt. Wer die Firmware weitergibt, muss die
Bedingungen der Ursprungslizenz einhalten.

## Übernommen aus NerdMiner_v2

Ursprung: <https://github.com/BitMaker-hub/NerdMiner_v2>
Lizenz: MIT, Copyright (c) 2023 Bitmaker — vollständiger Text in
`LICENSE-upstream`.

Betroffen sind:

- `components/bm24_sha_sw/` — der optimierte Software-SHA256d-Kernel,
  portiert aus `src/ShaTests/nerdSHA256plus.cpp`. Basiert seinerseits auf
  der shaLib von Blockstream Jade. Die Urheberangabe steht unverändert im
  Dateikopf.
- `components/bm24_media/` — die fünf Bildschirmgrafiken, übernommen aus
  demselben Projekt.

**Hinweis zu den Grafiken:** Sie tragen die Wortmarke „NerdMiner" im Bild.
Für eine eigenständige Produktoptik sollten sie durch eigene Entwürfe
ersetzt werden; die Markenfrage ist von der MIT-Lizenz des Codes nicht
abgedeckt.

## Eigener Anteil

Alles Übrige (Miner-Kern gegen das SHA-Werk, Stratum, Jobaufbereitung,
Netzwerk und Portal, Anzeige-Treiber, Konfiguration, Tests) ist für
BitMiner24 neu geschrieben.

**Offen:** Für diesen eigenen Anteil ist noch keine Lizenz festgelegt.
Ohne Angabe gilt gesetzliches Urheberrecht, also „alle Rechte
vorbehalten". Das ist für ein privates Repo unproblematisch, muss aber
entschieden werden, bevor die Firmware oder ihr Quelltext an Kunden oder
in die Öffentlichkeit geht.
