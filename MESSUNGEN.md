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

## 27.07.2026 — Hardware-SHA-Werk unter IDF 5.5 (offen)

| Pfad | Durchsatz | Status |
|---|---|---|
| roher Registerpfad aus 1.x | 57-65 kH/s im ersten Abschnitt, dann Stillstand | untauglich unter 5.5 |
| dokumentierte API (`esp_sha_block` + Modus explizit) | 8,2 kH/s | korrekt aufrufbar, aber 8x langsamer; eine Abweichung zur Referenz |

Belege fuer die Ursache des Stillstands:
- Ab IDF 5.5 setzen die SHA-Funktionen den Modus **nicht mehr implizit**,
  er muss vor jedem Block gesetzt werden
  (Migration Guide 5.5, Security).
- AES/SHA/MPI teilen sich Steuerregister; deren Zugriff wurde gekapselt
  und abgesichert (espressif/esp-idf@7761b0f). Der direkte
  `SHA_CONTINUE_REG`/`SHA_START_REG`-Pfad aus IDF 4.4 verletzt das.

Der Selbstschutz hat funktioniert wie gebaut: Die Abweichung wurde erkannt
und der HW-Pfad hat sich abgeschaltet, statt falsche Ergebnisse zu liefern.

### Was daraus folgt

Der Weg zu mehr Leistung fuehrt **ausschliesslich** ueber das SHA-Werk,
nicht ueber die CPU. Zu klaeren ist, wie der schnelle Registerpfad unter
IDF 5.5 sauber betrieben wird: Modus je Block explizit setzen, Zugriff auf
die geteilten Takt-/Reset-Register ueber die vorgesehenen Makros, und die
Aequivalenz danach wieder gegen die Referenz beweisen. Erst wenn der
Registerpfad unter 5.5 die 250+ kH/s der 1.x erreicht, ist der Umstieg
leistungsseitig neutral — der DMA-Modus waere der Schritt darueber hinaus.

Bis dahin bleibt 1.8.3-bm1 der Auslieferungsstand.
