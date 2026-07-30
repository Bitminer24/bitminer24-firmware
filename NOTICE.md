# Herkunft und Lizenzen

Diese Firmware ist ein Neubau auf ESP-IDF 5.5, enthält aber Bestandteile
aus dem Nerdminer-Projekt. Wer die Firmware weitergibt, muss die
Bedingungen der Ursprungslizenz einhalten.

## Übernommen aus Nerdminer_v2

Ursprung: <https://github.com/BitMaker-hub/NerdMiner_v2>
Lizenz: MIT, Copyright (c) 2023 Bitmaker — vollständiger Text in
`LICENSE-upstream`.

Betroffen sind:

- `components/bm24_sha_sw/` — der optimierte Software-SHA256d-Kernel,
  portiert aus `src/ShaTests/nerdSHA256plus.cpp`. Basiert seinerseits auf
  der shaLib von Blockstream Jade. Die Urheberangabe steht unverändert im
  Dateikopf.

**Hinweis zu den Grafiken:** Die aktuellen sieben 320×170-Layouts wurden für
BitMiner24 neu aufgebaut. Sie tragen weiterhin die Wortmarke „Nerdminer" im
Bild; die Markenfrage ist von der MIT-Lizenz des Codes nicht abgedeckt.

## Eigener Anteil

Alles Übrige (Miner-Kern gegen das SHA-Werk, Stratum, Jobaufbereitung,
Netzwerk und Portal, Anzeige-Treiber, Konfiguration, Tests) ist für
BitMiner24 neu geschrieben.

Der eigene Anteil steht ebenfalls unter der MIT-Lizenz. Der vollständige
Lizenztext befindet sich in `LICENSE`.
