# Messprotokoll — BitMiner24 Firmware 2.0

Alle Zahlen auf demselben LilyGO T-Display S3, ohne WLAN, fester Testheader,
Chip-Temperatursensor. Referenz ist die vermessene 1.8.3-bm1:
**292-298 kH/s bei 52-55 °C** (HW-Werk auf Kern 0 + SW-Kernel auf Kern 1).

## 27.07.2026 — Software-Kernel unter GCC 14 vs. GCC 8.4

| Aufbau | Kern 0 | Kern 1 | gesamt | Temp |
|---|---|---|---|---|
| SW-Kernel beide Kerne, IDF 5.5 / GCC 14.2, -O3 | 35,2 | 35,2 | 70,4 | 58-62 °C |
| 1.x Referenz, GCC 8.4 (SW nur Kern 1) | — | ~40 | — | — |

**Ergebnis: Die Compiler-These ist widerlegt.** Erwartet waren 80-95 kH/s je
Kern (2500-3000 Zyklen/Hash), gemessen sind 35 kH/s, also rund 6800
Zyklen/Hash — praktisch identisch zu GCC 8.4. Der Kernel ist bereits
vollstaendig ausgerollt; sechs Compiler-Generationen bringen daran nichts.
Damit ist die Hoffnung „SW-Kernel hebt uns auf 340-350 kH/s" vom Tisch,
und die Vermutung, NMMiners 384 kH/s kaemen aus einem schnelleren
SW-Anteil, ebenfalls unwahrscheinlich.

Nebenbefund: Zwei SW-Kerne unter Volllast erzeugen 58-62 °C, also **mehr**
Waerme als die 52-55 °C der 1.x mit HW-Werk — bei einem Viertel der
Leistung. Das HW-Werk ist nicht nur schneller, sondern auch deutlich
effizienter. Rechenlast auf die CPU zu verlagern ist thermisch der
schlechtere Weg.

## 27.07.2026 — Hardware-SHA-Werk unter IDF 5.5 (geloest)

| Pfad | HW | SW | gesamt | Status |
|---|---|---|---|---|
| fehlerhaftes H-Layout | 57,3 kH/s im angebrochenen ersten Messfenster | 35,7 | — | erster Treffer wich ab, Selbstabschaltung |
| korrigierter Registerpfad | **270,3 kH/s** | **35,6 kH/s** | **306,0 kH/s** | **64/64; 45-s-Nachlauf ohne Abweichung** |

Die vermeintlichen 57,3 kH/s waren keine reale Dauerleistung. Der Pfad
wurde beim ersten 16-Bit-Filtertreffer innerhalb des ersten
Sekundenfensters korrekt abgeschaltet; die Statistik zeigte nur den bis
dahin gerechneten Bruchteil.

### Ursache und Korrektur

Text- und H-Register sind beide als rohe little-endian CPU-Woerter
zugaenglich, aber ihre Daten bedeuten Verschiedenes:

- Die Textregister erhalten die vier Nachrichtenbytes roh aus dem Speicher.
- Die portable SHA-Referenz stellt einen Midstate als normale FIPS-u32 dar.
  Das H-Registerlayout erwartet diese Woerter byteverdreht.
- Gelesene H-Register duerfen fuer den Digest nicht nochmals als big-endian
  serialisiert werden. Ihr rohes little-endian Speicherbild sind bereits
  die richtigen 32 Digest-Bytes.

Der Fix dreht den portablen Midstate genau einmal pro Job in das
H-Registerlayout und kopiert den Enddigest roh. Im Nonce-Hot-Loop kam keine
Konvertierung hinzu.

Der Geraete-Boot-Selbsttest meldet danach:

```
Selbsttest HW-Werk: 64/64 == Referenz
```

Der anschliessende Lauf blieb bei 270,3 + 35,6 = 306,0 kH/s konstant.
Nach zusaetzlichen 45 Sekunden standen 441 hardwareseitige
16-Bit-Filtertreffer und null Abweichungen. Temperatur auf dem reinen
Dauerlast-Pruefstand: 59-60 °C.

### Was daraus folgt

Der IDF-5.5-Umstieg ist fuer den Mining-Kern nicht mehr
leistungsneutral, sondern liegt im reproduzierbaren Pruefstand etwa
8-14 kH/s ueber der 1.x-Gesamtreferenz. Mehr Takt wurde dafuer nicht
verwendet; der ESP32-S3 laeuft weiterhin mit 240 MHz.

Die 59-60 °C des pausenlosen Pruefstands sind korrekt, aber noch kein
Produkt-Abnahmewert. Im echten Miner kommen WiFi/Stratum/UI und eine
temperaturbasierte SW-Lastregelung hinzu. Ziel bleibt: 300+ kH/s ohne
thermisch unkontrollierte Optimierung. SHA-DMA bleibt ein spaeteres,
zeitlich begrenztes Experiment und ist keine Voraussetzung mehr fuer 2.0.

## 27.07.2026 — Nativer Job-/Miner-Pfad mit smarter SW-Dosierung

Der Messheader wird nicht mehr direkt im Benchmark erzeugt. Er durchlaeuft
jetzt den spaeteren Produktpfad:

```
mining.notify (IDF-cJSON)
  -> Coinbase + Merkle + nBits/Target
  -> atomarer Jobwechsel
  -> HW- und SW-Worker
  -> Referenzpruefung
  -> Share-Queue mit Difficulty/Netzwerkziel
```

Messung nach Race-Fix, 55 Sekunden seriell erfasst; die ersten drei
Anlaufwerte wurden aus dem Mittel entfernt:

| Messung | Ergebnis |
|---|---|
| stationaere Samples | 52 |
| mittlere Gesamtleistung | **300,26 kH/s** |
| typische 1-s-Werte | 299,0 bis 303,1 kH/s |
| HW-Werk | 270,3 kH/s |
| SW-Duty | 85-90 % |
| mittlere Temperatur | **60,7 °C** |
| HW-/SW-Kandidaten am Laufende | 218 / 35 |
| share-faehige Treffer | 21 |
| Abweichungen | **0** |

Die schwankenden 1-s-SW-Werte 28,7/32,8 kH/s sind Chunkgrenzen der
Duty-Regelung, kein instabiler Kernel. Ueber das Messfenster liegt die
Gesamtleistung knapp ueber 300 kH/s.

Thermische Einordnung: 300+ ist ohne Uebertaktung erreicht, aber die native
IDF-5.5-Sensormessung liegt mit 60-61 °C ueber den 52-55 °C der
1.x-Referenz. Ab 60 °C reduziert die Regelung nur den thermisch
ineffizienteren SW-Anteil; ab 63 °C stoppt sie ihn bis unter 59 °C. Das
effiziente Hardware-Werk bleibt bei 240 MHz. Der finale Grenzwert wird erst
mit WiFi, Display und Gehaeuse im Soak-Test festgelegt.

## 27.07.2026 — Produkt-RC1/RC2 auf COM21

Der feste Pruefstand wurde durch den nativen Produktpfad ersetzt und als
`2.0.0-rc1` geflasht und danach um die native Fuenf-Seiten-UI zum RC2
erweitert. Der Boot mit IDF 5.5.0 besteht NVS, nativen
I80/ST7789-Displaystart, Temperatursensor und SHA-Hardware-Selbsttest 64/64.
Ohne gespeicherte Konfiguration startet der WPA2-Setup-AP
`BitMiner24-62499D` mit HTTP-Portal.

Diese Beobachtung ist noch **keine neue Leistungszahl**. Eine belastbare
Produktmessung beginnt erst nach lokaler Eingabe von WLAN und echter
BTC-Adresse und muss Pool-Handshake, akzeptierte Shares, Wi-Fi-Reconnect,
Displaybetrieb, Temperaturregelung und den mehrstuendigen Soak einschliessen.

RC2 ergaenzt einen nativen 20-ms-Tastentask, PWM-Backlight mit 130/255,
Hintergrundmetriken fuer Preis/Netz/Solo und getrennte TLS-Zeitfenster.
Buildstand: 80.968 Byte RAM (24,7 %) und 1.103.997 Byte im App-Slot
(35,1 %). Die Informationsabrufe bleiben bis zur Provisionierung
hardwareseitig ungetestet.

## 27.07.2026 — Compiler- und Flash-Einstellungen: negatives Ergebnis

Anlass: Der Build lief unbemerkt mit `-Og` und im DIO-Flashmodus. Die
Speed-Doku von Espressif nennt fuer QIO gegenueber DIO nahezu doppelte
Ladegeschwindigkeit von Code aus dem Flash, und `-O2` statt `-Og` ist der
uebliche erste Griff.

Ursache des unbemerkten Zustands: PlatformIO erzeugt `sdkconfig.bm24-v2`
genau einmal und liest `sdkconfig.defaults` danach nie wieder. Aenderungen
an den Vorgaben wirken erst, wenn die Datei geloescht wird. Genau daran
war eine frueher gesetzte Optimierungsstufe stillschweigend gescheitert.
`tools/measure.sh` loescht sie deshalb vor jedem Lauf.

Je 150 s im laufenden Betrieb, gleiche Auswertung:

| Variante | Median | Mittel | Einbrueche | Temp |
|---|---|---|---|---|
| Ausgangsstand `-Og`, DIO | 299,0 | 291,1 | — | 53-56 °C |
| A: `-O2`, DIO | 303,1 | 287,4 | 4,2 % | 51-55 °C |
| B: `-O2` + QIO | 303,1 | 288,2 | 4,8 % | 51-55 °C |
| C: `-Os` + QIO | 303,1 | 288,3 | 4,2 % | 51-55 °C |

**Ergebnis: kein messbarer Unterschied.** Der Median steigt um vier
Einheiten, die Mittelwerte liegen innerhalb eines Kilohashes beieinander,
also im Rauschen. Zwischen `-O2` und `-Os` ist ebenfalls nichts zu sehen.

Erklaerung, und sie war vorhersagbar: Der Hot-Loop liegt im IRAM und wird
vom Peripheriebus zum SHA-Werk begrenzt, nicht vom Befehlsnachschub aus
dem Flash. Compiler- und Flash-Einstellungen koennen daran nichts aendern.

Behalten wird trotzdem `-Os` + QIO: der Binaerstand faellt von 52,8 % auf
50,3 % des App-Bereichs, der Start wird kuerzer, und Nachteile sind keine
gemessen worden.

Bleibende Erkenntnis: Wer hier mehr Leistung sucht, muss an das SHA-Werk
(DMA-Modus), nicht an die Uebersetzung.

## 27.07.2026 — SHA-DMA: erledigt, der Registerpfad gewinnt klar

Der letzte offene Leistungshebel, zeitlich begrenzt untersucht. Gemessen
wurde dieselbe Kette je Hash (Midstate laden, Header-Block 2 rechnen,
Digest lesen, zweiten SHA rechnen), einmal ueber Register und einmal ueber
`esp_sha_dma()`, je 2000 Durchlaeufe mit dem CPU-Taktzaehler:

| Weg | Takte je Hash | entspricht |
|---|---|---|
| Register (naive Fassung) | 1082 | 221,6 kH/s |
| DMA | 4681 | **51,3 kH/s** |

**DMA ist gut viermal langsamer.** Der Grund: DMA spielt seine Staerke nur
bei vielen Bloecken am Stueck aus. Unsere Kette braucht zwischen den zwei
Bloecken den Digest, also bleibt es bei einzelnen 64-Byte-Uebertragungen,
und deren Einrichtung kostet ein Vielfaches von sechzehn
Registerschreibvorgaengen.

Der Vergleich ist fuer DMA sogar noch guenstig gerechnet: Die
Register-Fassung im Versuch ist die naive Variante ohne
ZERO_TEXT_ONCE und ohne gefiltertes Digest-Lesen. Der echte Miner liegt
mit rund 890 Takten je Hash (270 kH/s) noch darunter.

**Damit ist die Leistungssuche abgeschlossen.** Getestet und verworfen sind
inzwischen: neuerer Compiler, kalibriertes Warten, Compiler-Optimierungs-
stufen, QIO-Flashmodus und DMA. Was blieb, war das Abschaffen der
Netzwerkpause (+10 kH/s). Der ESP32-S3 gibt ueber den Peripheriebus zum
SHA-Werk nicht mehr her. Der Code bleibt unter `BM24_BENCH_DMA=1`
reproduzierbar erhalten.
