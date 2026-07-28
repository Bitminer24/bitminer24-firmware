/* Dashboard fuer das Heimnetz.

   Bewusst eine einzige Seite ohne externe Abhaengigkeiten: sie fragt
   /status im Sekundentakt ab und rendert selbst. Damit ist das Geraet vom
   Sofa aus ablesbar, ohne Kabel und ohne aufs Display zu schauen. Auf dem
   alten Arduino-Unterbau war das nicht moeglich, weil der Webserver nur
   waehrend der Einrichtung lief.

   Liegt in einer eigenen Datei, weil die eingebetteten Backticks und
   Dollarzeichen des JavaScript in Werkzeugketten sonst leicht zerstoert
   werden. */

#ifndef BM24_DASHBOARD_HTML_H
#define BM24_DASHBOARD_HTML_H

static const char DASHBOARD_HTML[] =
"<!doctype html><html lang=de><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>BitMiner24</title><style>"
"body{font:16px system-ui;background:#071018;color:#eef;max-width:40rem;"
"margin:1.5rem auto;padding:0 1rem}h1{color:#f7931a;margin:.2rem 0}"
".g{display:grid;grid-template-columns:repeat(auto-fit,minmax(9rem,1fr));"
"gap:.7rem;margin:1rem 0}.c{background:#0e1a24;border:1px solid #24384a;"
"border-radius:.5rem;padding:.7rem}.k{color:#8fa8bd;font-size:.78rem;"
"text-transform:uppercase;letter-spacing:.04em}"
".v{font-size:1.35rem;font-weight:700;margin-top:.2rem}"
"a{color:#f7931a}</style>"
"<h1>BitMiner24</h1><div id=w>Lade ...</div>"
"<p><a href=/setup>Einstellungen und Firmware-Update</a></p>"
"<script>\n"
"function card(k,v){return '<div class=c><div class=k>'+k+"
"'</div><div class=v>'+v+'</div></div>';}\n"
"function tick(){\n"
"  fetch('/status').then(function(r){return r.json();}).then(function(s){\n"
"    document.getElementById('w').innerHTML='<div class=g>'+\n"
"      card('Hashrate',s.khs.toFixed(1)+' kH/s')+\n"
"      card('Temperatur',s.temp.toFixed(1)+' C')+\n"
"      card('Shares',s.accepted+'/'+s.submitted)+\n"
"      card('Block gefunden',s.blocks_found)+\n"
"      card('Beste Difficulty',s.best.toFixed(6))+\n"
"      card('Pool',s.pool?'online':'getrennt')+\n"
"      card('Laufzeit',s.uptime)+\n"
"      card('Worker am Pool',s.workers)+\n"
"      card('Version',s.version)+'</div>';\n"
"  }).catch(function(){});\n"
"}\n"
"tick();setInterval(tick,2000);\n"
"</script></html>";

#endif /* BM24_DASHBOARD_HTML_H */
