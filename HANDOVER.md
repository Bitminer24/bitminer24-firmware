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

## Was hängt: der Hardware-SHA-Kernel unter IDF 5.5

Das ist der einzige offene Punkt, aber der entscheidende.

| Variante | Durchsatz Kern 0 | Korrektheit |
|---|---|---|
| 1.x zum Vergleich (IDF 4.4) | ~250 kH/s | 64/64 gegen mbedtls |
| roher Registerpfad, portiert | **57,3 kH/s** | **1 Abweichung** → Selbstabschaltung |
| dokumentierte `esp_sha_block`-API | 8,2 kH/s | ebenfalls 1 Abweichung |

Zwei Symptome, vermutlich dieselbe Ursache:
1. **Faktor 4 zu langsam** gegenüber 1.x, obwohl derselbe Registerpfad.
2. **Genau eine Abweichung** pro Lauf, bei 0 bestätigten Kandidaten. Die
   Abweichung tritt beim ersten Filtertreffer auf (1 von 65536), das heißt
   die Hashes stimmen vermutlich generell nicht.

### Nächster Schritt, der Klarheit bringt

In `main/app_main.c` ist der HW-Selbsttest gerade eingebaut worden
(`bm24_sha_hw_selftest(64)`), **aber noch nie gelaufen**. Ein Flash und ein
Blick auf die Zeile „Selbsttest HW-Werk:" entscheidet alles:

- **FEHLGESCHLAGEN** → der Registerpfad rechnet grundsätzlich falsch, dann ist
  es die Byte-Reihenfolge (siehe unten). Das ist die wahrscheinliche Antwort.
- **64/64** → die Hashes stimmen, dann liegt der Fehler im Filter/Leseweg
  (`ll_read_digest_if`).

```bash
cd /pfad/zum/Desktop/bitminer24-firmware-v2
export PATH="/pfad/zum/AppData/Local/Python/bin:$PATH"
python -m platformio run -e bm24-v2 -t upload --upload-port COM21
# dann seriell mitlesen, 115200
```

### Konkrete Verdachtsmomente

1. **Byte-Reihenfolge der Textregister.** Die Konstanten in 1.x verraten, dass
   die Textregister die Nachrichtenwörter byteverdreht nehmen (`0x00000080`
   statt `0x80000000`, Länge 640 als `0x80020000`), die H-Register dagegen
   normal. Ich habe zuerst big-endian konvertiert (falsch), dann roh per
   `memcpy` übernommen. Zu prüfen ist, wie 1.x den Puffer wirklich füllt:
   in `mining.cpp` wird `job->sha_buffer` an `nerd_sha_ll_fill_text_block_sha256`
   übergeben — nachsehen, ob dieser Puffer vorher schon byteverdreht wurde.
   Das ist der Kern der Sache.

2. **Registerzugriff.** 1.x liest die H-Register mit
   `DPORT_SEQUENCE_REG_READ`, v2 aktuell mit einfachem `REG_READ`. Auf dem S3
   sollte das gleichwertig sein, ist aber nicht verifiziert.

3. **Faktor 4.** Selbst wenn die Korrektheit stimmt, fehlt der Durchsatz.
   Verdächtig ist `hw_begin()` je 8192er-Abschnitt (`esp_sha_acquire_hardware`
   setzt seit 5.5 den Baustein zurück) und der zusätzliche Digest-Lesevorgang
   pro Hash.

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
