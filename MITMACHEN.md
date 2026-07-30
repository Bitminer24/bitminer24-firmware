# Mitmachen

## Werkzeuge

- PlatformIO (`pip install platformio`), holt ESP-IDF 5.5 und GCC 14.2 selbst
- für die Host-Tests ein PC-GCC, unter Windows z. B.
  `winget install BrechtSanders.WinLibs.POSIX.UCRT`

## Bauen, flashen, messen

```bash
pio test -e native                                  # 38 Host-Tests, ohne Hardware
pio run -e bm24-v2                                  # Firmware bauen
pio run -e bm24-v2 -t upload --upload-port <PORT>   # Port z. B. COM4
BM24_PORT=<PORT> ./tools/measure.sh "Variante" 150  # bauen, flashen, messen
```

**Stolperstein:** PlatformIO schreibt `sdkconfig.bm24-v2` genau einmal und
liest `sdkconfig.defaults` danach nie wieder. Wer an den Vorgaben etwas
ändert, muss die Datei löschen, sonst wirkt die Änderung stillschweigend
nicht. `tools/measure.sh` erledigt das automatisch. Genau daran ist hier
schon einmal eine Optimierungsstufe unbemerkt gescheitert.

## Erste Einrichtung am Gerät

Ohne gespeicherte Konfiguration öffnet das Gerät das WLAN `NerdminerAP`
mit dem Passwort `MineYourCoins`, dem bekannten Standardwert des
Ursprungsprojekts. Verbinden, die Anmeldeseite öffnet sich von selbst
(sonst `http://192.168.4.1`), eigenes WLAN aus der Liste wählen und die
eigene Bitcoin-Adresse eintragen.

Die Adresse ist bewusst nicht vorbelegt. Ein Fund geht immer an die
hinterlegte Adresse und lässt sich danach nicht mehr umleiten.

Im Heimnetz zeigt das Gerät unter seiner IP ein Dashboard. Änderungen
verlangen dort ein Passwort, das je Gerät aus der MAC abgeleitet wird und
auf der Einrichtungsseite sowie beim Start im seriellen Protokoll steht. Es ist
absichtlich **nicht** das WLAN-Passwort, denn das ist öffentlich bekannt
und würde jedem im Netz erlauben, die Konfiguration zu verändern.

Firmware-Updates werden ausschließlich über den verlinkten
BitMiner24-Web-Updater installiert. Die lokale Oberfläche besitzt weder
einen Datei-Upload noch einen `/ota`-Endpunkt.

Bedienung: Seitentaste kurz blättert, vier Sekunden öffnet die
Einrichtung, zehn Sekunden setzt auf Werkszustand zurück. Zweite Taste
schaltet das Display, Doppelklick dreht es.

## Eigene Grafiken

Die Bildschirme liegen als RGB565-Felder in
`components/bm24_media/bm24_media.c`, jeweils 320×170. Die Quellen dazu
stehen unter `assets/screens-320x170/`.

1. SVG unter `assets/screens-320x170/source-svg/` in 320×170 anlegen.
   Beschriftungen gehören ins Bild, die Kästchen für die Werte bleiben leer.
2. Mit `source-svg/render.ps1` die PNGs erzeugen, danach mit
   `embed-screens.ps1` als RGB565 einbetten.
3. Die Koordinaten der Werte stehen in `components/bm24_ui/bm24_ui.c` bei
   der jeweiligen Seite als `frame->slot[n] = {x, y, Größe, rechtsbündig}`.
   `x` ist bei rechtsbündig die **rechte** Kante des Kästchens.

## Bevor du eine Optimierung baust

Bitte [`MESSUNGEN.md`](MESSUNGEN.md) lesen. Dort steht mit Zahlen, was
bereits ausprobiert und verworfen wurde, darunter neuerer Compiler,
Optimierungsstufen, QIO-Flashmodus und der DMA-Modus des SHA-Werks. Das
spart dir einen Tag.
