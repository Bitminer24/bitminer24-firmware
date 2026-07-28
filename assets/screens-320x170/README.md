# NerdMiner² Screens — 320×170

Diese sieben Grafiken sind nativ für das 320×170-Pixel-Display aufgebaut.
Sie sind keine verkleinerten 1600×800-Fotos: Texte, Linien und Felder werden
direkt in der Zielauflösung gerendert. Unwichtige und mehrfach vorhandene
Informationen wurden entfernt, damit die Kernwerte lesbar bleiben.

| Datei | Kerninhalt | Firmware-Array |
|---|---|---|
| `setup.png` | WLAN, Passwort, Verbindungs-QR | `bm24_img_setup` |
| `miner.png` | Hashrate, bestätigte Blocktreffer, Hashes, Temperatur, Anteile, beste Diff., Laufzeit | `bm24_img_miner` |
| `init.png` | Start- und WLAN-Status | `bm24_img_init` |
| `clock.png` | Uhrzeit, Block, Preis, Hashrate | `bm24_img_clock` |
| `network.png` | Netzwerk-Hashrate, Block, Halving, Gebühr, Schwierigkeit | `bm24_img_network` |
| `price.png` | Bitcoin-Preis, Hashrate, Block | `bm24_img_price` |
| `solo.png` | Letzter Solo-Block, Jackpot, Statistik, Retarget | `bm24_img_solo` |

## Setup-QR

Der QR-Code ist absichtlich eine eigene, scharf gerenderte Rastermatrix mit
drei Pixeln pro Modul. Er enthält unverändert:

```text
WIFI:S:NerdMinerAP;T:WPA;P:MineYourCoins;;
```

Die Firmware verwendet dazu passend `NerdMinerAP`, WPA2-PSK und das Passwort
`MineYourCoins`. Der finale PNG-Export und ein RGB565-Rückexport wurden mit
ZXing erfolgreich dekodiert.

## Quellen und Einbetten

- Editierbare Layouts: `source-svg/*.svg`
- QR-Quelle: `source-svg/qr-wifi.png`
- PNGs neu rendern: `source-svg/render.ps1`
- PNGs als RGB565 in die Firmware einbetten: `embed-screens.ps1`

Die Live-Werte und ihre Koordinaten stehen in
`components/bm24_ui/bm24_ui.c`. Darum müssen Änderungen an den leeren
Wertefeldern immer mit diesen Slots abgestimmt werden.

`BLOCK GEFUNDEN` ist ein dauerhafter Lebenszeitzähler. Er steigt nur, wenn
ein Hash das Bitcoin-Netzwerkziel erreicht und genau dieser Submit vom Pool
bestätigt wurde. Normale angenommene Shares und die aktuelle Blockhöhe werden
nicht mitgezählt.
