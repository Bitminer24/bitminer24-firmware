# Übergabe — Stand 27.07.2026

## WICHTIG ZUERST: Auf COM21 laeuft jetzt der IDF-5.5-Produkt-RC

Auf dem LilyGO T-Display S3 liegt **BitMiner24 2.0.0-rc2**, kein fester
Pruefstand und keine 1.x-Firmware. Der Boot ist sauber, Display und Setup-AP
laufen, der SHA-Hardware-Selbsttest meldet 64/64. Das Geraet mint noch nicht,
weil nach dem Factory-Flash absichtlich keine privaten Zugangsdaten im NVS
liegen.

Ersteinrichtung direkt am Handy oder Rechner:

1. WLAN `NerdMinerAP`, Passwort `MineYourCoins` (oder QR-Code scannen)
2. `http://192.168.4.1`
3. eigenes WLAN und echte BTC-Adresse/Worker eintragen
4. fuer `public-pool.io` zunaechst Port `3333`, TLS aus, Passwort `x`

Danach muss COM21 fuer die echte Abnahme mitgelesen werden: Connect,
Subscribe, Authorize, Job, circa 300 kH/s, Temperaturregelung und mindestens
ein akzeptierter Share. Erst nach diesem Lauf und einem mehrstuendigen Soak
wird aus RC2 ein Release.

Nur fuer den Notfall kann 1.8.3 wiederhergestellt werden:

```bash
cd "/pfad/zum/Desktop/CryptoTuts Website/nerdminer-flasher-dev"
python -m esptool --chip esp32s3 --port COM21 --baud 921600 \
  write_flash 0x0 NerdminerV2_factory.bin
```

Ein Factory-Flash ab `0x0` loescht NVS; ein spaeteres App-/OTA-Update nicht.

## Zwei Bäume

| | Pfad | Rolle |
|---|---|---|
| 1.8.3-bm1 | `CryptoTuts Website/nerdminer-firmware` | Rueckfallstand, Arduino/IDF 4.4, 292-298 kH/s @ 52-55 °C |
| 2.0 | `Desktop/bitminer24-firmware-v2` | **Aktiver RC auf COM21**, nativ IDF 5.5, Produkt-Soak noch offen |

v2 liegt außerhalb des Workspace, weil der IDF-Build Pfade mit Leerzeichen ablehnt.

## Was fertig und bewiesen ist (v2)

- ESP-IDF 5.5.0 / GCC 14.2, bootet auf dem Gerät
- A/B-OTA-Partitionen aktiv, App ab `0x10000`, Boot-Rollback an
- **38/38 Host-Tests grün** (`pio test -e native`): Konfiguration,
  Formatierung, SHA-Referenz
  gegen NIST + Genesis, Midstate-Äquivalenz, SW-Kernel-Filter über 800k
  Nonces, native Stratum-Jobaufbereitung und JSON-RPC-Drahtformat
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

## Native Jobaufbereitung nach dem SHA-Fix

`components/bm24_work` ist die erste Schicht des echten Miner-Ports:

- Coinbase aus coinb1 + extranonce1 + extranonce2 + coinb2
- Merkle-Pfad mit doppeltem SHA
- exakt dieselbe Header-Byteordnung wie der vermessene 1.x-Miner
- nBits -> little-endian 256-Bit-Netzwerkziel
- Share-Difficulty und exakter Zielvergleich ohne Arduino-Typen
- feste Grenzen fuer Coinbase und Merkle-Zweige, klare Fehler statt
  ueberlaufender Puffer

Die Erwartungswerte im Host-Test wurden unabhaengig mit Python `hashlib`
erzeugt. Dazu kommen Genesis-Hash gegen diff-1-Target und Fehlerfaelle fuer
Hex, extranonce2 und compact target.

`components/bm24_stratum` ersetzt Arduino-`String`, `WiFiClient` und
ArduinoJson an der Protokollgrenze:

- begrenzte, besitzende Jobstrukturen
- sichere JSON-RPC-Writer mit Escaping und Fail-closed-Puffern
- Nonce immer achtstellig (1.x liess fuehrende Nullen weg)
- IDF-cJSON fuer notify, set_difficulty, set_extranonce und Antworten

`components/bm24_miner` ist der echte native Rechenkern:

- atomarer Jobwechsel; Race zwischen Generation und Midstate geschlossen
- getrennte Startbereiche fuer HW-/SW-Nonces
- jeder HW- und SW-Kandidat gegen die portable Referenz
- nur verifizierte Shares gelangen in die Queue
- 8192er-HW-Chunks und 20-s-Rueckfalltuer fuer TLS-Netzfenster
- SW-Duty 0-100 %, das effizientere HW-Werk wird thermisch nicht gedrosselt

Geraetelauf ueber den kompletten notify->Share-Pfad: 52 stationaere
1-s-Samples, **300,26 kH/s im Mittel**, 60,7 °C, 218/35 Kandidaten,
21 share-faehige Treffer, **0 Abweichungen**. RAM 6,2 %, Flash 8,1 %.
Stand vor RC-Rollout: 38/38 Host-Tests gruen.

### Produktpfad im RC2

Der feste `mining.notify`-Pruefstand ist aus `app_main` entfernt. Der Build
enthaelt nun:

- versionierten NVS-Blob mit Validierung und Sperre fuer Platzhalter/Burn-Adresse
- nativen Wi-Fi-STA-Pfad mit Event-Reconnect und WPA2-Setup-AP
- lokales HTTP-Portal fuer WLAN, Worker, Pool und Firmware-Update
- Stratum TCP oder TLS mit Zertifikatsbundle, SNTP, Keepalive und Backoff
- Subscribe/Authorize, Notify/Difficulty/Extranonce, Jobwechsel und Share-Submit
- nativen IDF-I80/ST7789-Treiber auf den offiziellen T-Display-S3-Pins
- PWM-Backlight mit der bewaehrten 130/255-Helligkeit, ohne Hashverlust
- fuenf native Seiten wie im 1.x-Ablauf: Miner, Uhr/Block/Preis,
  Bitcoin-Netz, grosser BTC-Preis und Solo-Tracker
- zeitlich entzerrte HTTPS-Abrufe fuer CoinGecko, mempool.space und den
  BitMiner24-Solo-Tracker; nie HTTP im Zeichenpfad
- nativen 20-ms-Tastentask: GPIO 14 kurz = naechste Seite, vier Sekunden =
  Setup; GPIO 0 kurz = Display an/aus, doppelt = drehen
- A/B-OTA; ein neues Image wird erst nach NVS-, Display-, SHA- und
  Netzwerk/Portal-Selbsttest als gueltig markiert
- Supervisor-Watchdog, HW-Stall-Erkennung, SHA-Fail-closed und smarte
  SW-Temperaturdosierung
- GPIO 14: vier Sekunden halten oeffnet das Setup-Portal erneut

Es gibt im v2-Produktbuild keine Arduino-, WiFiManager-, ArduinoJson- oder
TFT_eSPI-Abhaengigkeit mehr. Der reale Poolpfad ist implementiert, aber noch
nicht mit den echten Zugangsdaten des Besitzers end-to-end vermessen.

### Verifikation des RC2

Bereits bestanden:

- 38/38 Host-Tests
- IDF-5.5.0/GCC-14.2-Firmwarebuild
- RAM 80.968 / 327.680 Byte (24,7 %)
- App 1.103.997 / 3.145.728 Byte (35,1 %)
- Flash auf COM21
- nativer LCD-Start, Temperatursensor, SHA-Selbsttest 64/64
- WPA2-Setup-AP und HTTP-Server laut Geraetelog
- sauberer Betrieb ohne Task-Watchdog-Fehler

Noch offen und nicht schoenreden:

- lokale Provisionierung mit echtem WLAN und echter BTC-Adresse
- echter Pool-Handshake und akzeptierter Share
- Live-Pruefung aller fuenf Seiten, Marktdaten und beider Tasten am Geraet
- Durchsatz/Temperatur mit Wi-Fi, Display und Gehaeuse
- Reconnect-, OTA-Rollback- und mehrstuendiger Soak-Test auf Hardware

Die fuenf Informationsseiten sind funktional nativ portiert und neu
gezeichnet. Sie verwenden bewusst keine TFT_eSPI-/Arduino-Bitmaps; die
alten Hintergrundgrafiken sind daher nicht pixelidentisch. Falls
Pixelgleichheit statt der nativen Neugestaltung gewuenscht ist, ist das ein
separater visueller Port, kein fehlender Mining- oder Bedienpfad.

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

## Alter 1.x-Baum

- `main` liegt 18 Commits vor `origin/main`; **nichts gepusht**.
- Der Knopf-Fix (Seitenwechsel hing >30 s) ist geflasht, aber **physisch noch
  nicht getestet**. Er gehoert nur zum Rueckfallstand.
- Die v2-Konfigurationsvalidierung lehnt
  `1BitcoinEaterAddressDontSendf59kuE` und `yourBtcAddress` ab.
