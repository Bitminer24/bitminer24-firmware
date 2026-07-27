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

## 27.07.2026 — Produkt-RC1 auf COM21

Der feste Pruefstand wurde durch den nativen Produktpfad ersetzt und als
`2.0.0-rc1` geflasht. Der Boot mit IDF 5.5.0 besteht NVS, nativen
I80/ST7789-Displaystart, Temperatursensor und SHA-Hardware-Selbsttest 64/64.
Ohne gespeicherte Konfiguration startet der WPA2-Setup-AP
`BitMiner24-62499D` mit HTTP-Portal.

Diese Beobachtung ist noch **keine neue Leistungszahl**. Eine belastbare
Produktmessung beginnt erst nach lokaler Eingabe von WLAN und echter
BTC-Adresse und muss Pool-Handshake, akzeptierte Shares, Wi-Fi-Reconnect,
Displaybetrieb, Temperaturregelung und den mehrstuendigen Soak einschliessen.
