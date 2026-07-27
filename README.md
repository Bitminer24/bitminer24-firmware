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

Darauf sitzt jetzt der vollstaendige native Produktpfad: versionierte
NVS-Konfiguration, WPA2-Setup-AP, Wi-Fi STA mit Reconnect, Stratum ueber
TCP oder geprueftes TLS, Job-/Share-Verarbeitung, natives `esp_lcd`-Display,
A/B-OTA mit Rollback, Watchdog und Temperaturregelung. Arduino,
WiFiManager, ArduinoJson und TFT_eSPI sind nicht Teil des v2-Builds.

Der komplette Rechenpfad erreicht im 55-s-Pruefstand durchschnittlich
300,26 kH/s bei 60,7 °C und null Abweichungen. Der Produkt-RC
`2.0.0-rc1` bootet auf dem T-Display S3 und wartet aktuell auf seine erste
lokale Provisionierung. Poolbetrieb, Shares und Temperatur mit echtem
Wi-Fi/Display werden danach im Soak-Test abgenommen; bis dahin ist RC1
bewusst noch kein finales Release.

## Bauen

```bash
pio run -e bm24-v2          # Geräte-Firmware (ESP-IDF 5.5, GCC 14.2)
pio test -e native          # Host-Tests, laufen auch in der CI
```

Flashen ab `0x10000` (App-only) erhaelt die NVS-Konfiguration. Ein kompletter
Factory-Flash ab `0x0` loescht sie.

## Ersteinrichtung

1. Mit `BitMiner24-<Chip-ID>` verbinden, Passwort `MineYourCoins`.
2. `http://192.168.4.1` oeffnen.
3. WLAN, echte BTC-Adresse/Worker und Pool eintragen.

`yourBtcAddress` und die bekannte Burn-Adresse werden absichtlich
abgelehnt. GPIO 14 vier Sekunden halten oeffnet das Portal spaeter erneut.

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
  bm24_config/       versionierter NVS-Blob und Fail-closed-Validierung
  bm24_network/      Wi-Fi, Setup-Portal und A/B-OTA-Transport
  bm24_pool/         TCP/TLS, SNTP, Reconnect, Jobs und Share-Submit
  bm24_display/      nativer I80/ST7789-Treiber fuer T-Display S3
test/                38 Unity-Host-Tests (pio test -e native)
partitions.csv       A/B-OTA-Layout, App ab 0x10000
```
