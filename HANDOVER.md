# Übergabe — Stand 27.07.2026

## WICHTIG ZUERST: Das Testgerät mint gerade NICHT

Auf dem LilyGO T-Display S3 (COM21) liegt aktuell die **v2-Prüfstand-Firmware**,
nicht der Miner. Zum Zurückholen des Produktivstands:

```bash
cd "/pfad/zum/Desktop/CryptoTuts Website/nerdminer-flasher-dev"
python -m esptool --chip esp32s3 --port COM21 --baud 921600 \
  write_flash 0x0 NerdminerV2_factory.bin
```

Danach WLAN und BTC-Adresse einmal im Portal eintragen (Factory-Flash löscht NVS).

## Zwei Bäume

| | Pfad | Rolle |
|---|---|---|
| 1.8.3-bm1 | `CryptoTuts Website/nerdminer-firmware` | **Auslieferungsstand**, Arduino/IDF 4.4, 292-298 kH/s @ 52-55 °C |
| 2.0 | `Desktop/bitminer24-firmware-v2` | Neubau auf IDF 5.5, Mining-Kern noch nicht abgenommen |

v2 liegt außerhalb des Workspace, weil der IDF-Build Pfade mit Leerzeichen ablehnt.

## Was fertig und bewiesen ist (v2)

- ESP-IDF 5.5.0 / GCC 14.2, bootet auf dem Gerät
- A/B-OTA-Partitionen aktiv, App ab `0x10000`, Boot-Rollback an
- **19/19 Host-Tests grün** (`pio test -e native`): Formatierung, SHA-Referenz
  gegen NIST + Genesis, Midstate-Äquivalenz, SW-Kernel-Filter über 800k Nonces
- CI-Workflow (Host-Tests + Firmware-Build) geschrieben, noch nicht gepusht
- Host-Tests brauchen MinGW-GCC (per winget installiert), Pfad:
  `~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT*/mingw64/bin`

## Hardware-SHA-Kernel unter IDF 5.5: am 27.07.2026 geloest

Der entscheidende Phase-2-Blocker ist beseitigt.

| Variante | Kern 0 | Kern 1 | gesamt | Korrektheit |
|---|---|---|---|---|
| 1.x zum Vergleich (IDF 4.4) | ~250 kH/s | ~40 kH/s | 292-298 kH/s | 64/64 gegen mbedtls |
| IDF-5.5-Registerpfad, korrigiert | **270,3 kH/s** | **35,6 kH/s** | **306,0 kH/s** | **64/64 + Laufzeitpruefung gruen** |

Der vorherige Wert 57,3 kH/s war nur der angebrochene erste
Sekundenabschnitt bis zur Selbstabschaltung, kein echter Durchsatz.

Ursache: `bm24_sha_midstate()` liefert normale FIPS-u32, die H-Register
erwarten auf dem little-endian S3 aber jedes Wort byteverdreht. Umgekehrt
waren die gelesenen H-Woerter nochmals big-endian serialisiert worden,
obwohl ihr rohes Speicherbild bereits der korrekte Digest ist. Der Fix:

1. Midstate einmal pro Job per `__builtin_bswap32` ins H-Registerlayout.
2. Enddigest per `memcpy`, keine zweite Bytekonvertierung.
3. Keine Zusatzarbeit im Nonce-Hot-Loop.

Geraetemessung:

```
Selbsttest HW-Werk: 64/64 == Referenz
[bench] Kern0 270.3 kH/s, Kern1 35.6 kH/s, gesamt 306.0 kH/s
```

Weitere 45 Sekunden: 441 HW-Filtertreffer, null Abweichungen, konstant
306,0 kH/s, 59-60 °C auf dem pausenlosen Pruefstand.

```bash
cd /pfad/zum/Desktop/bitminer24-firmware-v2
export PATH="/pfad/zum/AppData/Local/Python/bin:$PATH"
python -m platformio run -e bm24-v2 -t upload --upload-port COM21
# dann seriell mitlesen, 115200
```

### Naechster Produkt-Schritt

Der aktuelle v2-Build ist weiterhin ein reproduzierbarer Pruefstand, noch
kein Miner. Jetzt Stratum/Jobaufbereitung/Validierung als native,
host-testbare Komponenten portieren; danach WiFi/NVS/UI. Den schnellen
Registerpfad dabei unveraendert als abgenommene Basis behandeln.

### Quellen, die schon geprüft sind

- Ab IDF 5.5 setzen die SHA-Funktionen den Modus **nicht mehr implizit**:
  Migration Guide 5.5, Security
- AES/SHA/MPI teilen sich Steuerregister, Zugriff wurde gekapselt:
  espressif/esp-idf@7761b0f

## Wichtigstes inhaltliches Ergebnis (bitte nicht neu erarbeiten)

**Der Software-Kernel ist kein Hebel.** Unter GCC 14.2 mit -O3 liefert er
35,2 kH/s pro Kern (~6800 Zyklen/Hash), praktisch identisch zu GCC 8.4. Die
These „neuer Compiler bringt 80-95 kH/s pro Kern" ist damit widerlegt.

**Und er ist thermisch der schlechtere Weg:** zwei SW-Kerne unter Volllast
erzeugen 58-62 °C, also mehr als die 52-55 °C der 1.x mit HW-Werk — bei einem
Viertel der Leistung. Reserve steckt ausschließlich im SHA-Werk, nie in der CPU.

Details in `MESSUNGEN.md`.

## Offene Punkte im 1.x-Baum

- Commits `7bfa45d` (TLS/SHA-Sperre, Solo-Screen), `13dd532` (Knopf-Task),
  `fc0182d`, `a67140f` liegen lokal. **Nichts gepusht**, wartet auf OK.
- Der Knopf-Fix (Seitenwechsel hing >30 s) ist geflasht, aber **physisch noch
  nicht getestet**. Erwartung: Wechsel unter einer Sekunde.
- Testkonfiguration nutzt die Burn-Adresse `1BitcoinEaterAddressDontSendf59kuE`.
  Vor jedem Release muss ein Check das verhindern.
