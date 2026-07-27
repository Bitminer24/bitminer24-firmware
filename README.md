# BitMiner24 Firmware 2.0

Natives ESP-IDF 5.5 (via PlatformIO, `framework = espidf`), modular
geschnitten, host-getestet. Plan und Begründung:
`Desktop/CryptoTuts Website/nerdminer-firmware/docs/FIRMWARE-2.0-PLAN.md`.

Dieses Repo liegt bewusst AUSSERHALB des CryptoTuts-Workspace: der
IDF-Build verweigert Projektpfade mit Leerzeichen, und der Workspace-Ordner
heißt "CryptoTuts Website".

Die vermessene Referenz bleibt die 1.8.3-bm1 im Arduino-Baum
(`CryptoTuts Website/nerdminer-firmware/`): 292-298 kH/s bei 52-55 °C auf
dem LilyGO T-Display S3. Jede Phase von 2.0 wird gegen diese Zahlen
abgenommen.

## Bauen

```bash
pio run -e bm24-v2          # Geräte-Firmware (ESP-IDF 5, GCC 13)
pio test -e native          # Host-Tests, laufen auch in der CI
```

Flashen ab `0x10000` (App-only) erhält die NVS-Konfiguration, wie bei 1.x.

## Struktur

```
main/                app_main, Task-Aufbau
components/
  bm24_format/       Formatierung (Tausenderpunkte, Altersangaben) — host-getestet
  (folgt) bm24_sha/  bm24_stratum/ bm24_validate/ bm24_net/ bm24_ui/
                     bm24_config/ bm24_ota/
test/                Unity-Host-Tests (pio test -e native)
partitions.csv       A/B-OTA-Layout, App ab 0x10000
```
