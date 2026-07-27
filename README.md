# BitMiner24 Firmware 2.0

Natives ESP-IDF 5.5 (via PlatformIO, `framework = espidf`), modular
geschnitten, host-getestet. Plan und Begründung:
`Desktop/CryptoTuts Website/nerdminer-firmware/docs/FIRMWARE-2.0-PLAN.md`.

Dieses Repo liegt bewusst AUSSERHALB des CryptoTuts-Workspace: der
IDF-Build verweigert Projektpfade mit Leerzeichen, und der Workspace-Ordner
heißt "CryptoTuts Website".

Die vermessene Referenz bleibt die 1.8.3-bm1 im Arduino-Baum
(`CryptoTuts Website/nerdminer-firmware/`): 292-298 kH/s bei 52-55 °C auf
dem LilyGO T-Display S3. Der native IDF-5.5-SHA-Pruefstand erreicht nach
Korrektur des H-Registerlayouts reproduzierbar 306,0 kH/s (270,3 HW +
35,6 SW), 64/64 Boot-Vektoren und Millionen Hashes ohne Abweichung.

Darauf sitzen inzwischen die native Stratum-Drahtschicht, die
Coinbase/Merkle/Target-Aufbereitung und ein echter HW/SW-Miner-Task mit
atomarem Jobwechsel, verifizierter Share-Queue, TLS-Netzfenster und
temperaturabhaengiger SW-Dosierung. Der komplette Geraetepfad erreicht im
55-s-Lauf durchschnittlich 300,26 kH/s bei 60,7 °C und null Abweichungen.

Noch nicht Produktfirmware sind WiFi/NVS-Provisioning, der Pool-Socket,
UI und OTA-Transport. Bis diese Schichten portiert und im Soak-Test
abgenommen sind, bleibt 1.8.3-bm1 der Auslieferungsstand.

## Bauen

```bash
pio run -e bm24-v2          # Geräte-Firmware (ESP-IDF 5.5, GCC 14.2)
pio test -e native          # Host-Tests, laufen auch in der CI
```

Flashen ab `0x10000` (App-only) erhält die NVS-Konfiguration, wie bei 1.x.

## Struktur

```
main/                app_main, Task-Aufbau
components/
  bm24_format/       Formatierung (Tausenderpunkte, Altersangaben) — host-getestet
  bm24_sha/          portable SHA-Referenz — host-getestet
  bm24_sha_sw/       ausgerollter Software-Mining-Kernel — host-getestet
  bm24_sha_hw/       ESP32-S3-SHA-Werk, Boot-Selbsttest und Laufzeitpruefung
  bm24_miner/        HW/SW-Worker, Jobwechsel, Share-Queue, Netzfenster,
                     thermisch dosierbarer SW-Anteil
  bm24_work/         Stratum-Job -> Coinbase/Merkle/Header/Target,
                     Share-Difficulty und Zielvergleich — host-getestet
  bm24_stratum/      sichere JSON-RPC-Ausgabe, IDF-cJSON-Parser,
                     mining.notify/set_difficulty/set_extranonce
  (folgt) bm24_net/ bm24_ui/ bm24_config/ bm24_ota/
test/                Unity-Host-Tests (pio test -e native)
partitions.csv       A/B-OTA-Layout, App ab 0x10000
```
