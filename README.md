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

Das ist die abgenommene Mining-Kern-Basis, noch nicht die komplette
Produktfirmware: Stratum, Konfiguration, UI und Netz werden anschliessend
als native Komponenten portiert.

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
  bm24_work/         Stratum-Job -> Coinbase/Merkle/Header/Target,
                     Share-Difficulty und Zielvergleich — host-getestet
  (folgt) bm24_stratum/ bm24_net/ bm24_ui/ bm24_config/ bm24_ota/
test/                Unity-Host-Tests (pio test -e native)
partitions.csv       A/B-OTA-Layout, App ab 0x10000
```
