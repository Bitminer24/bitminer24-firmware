/* Gemeinsame BitMiner24-CI fuer Dashboard und Setup-Portal.

   Alles bleibt inline und ohne externe Schrift-, Bild- oder Scriptabrufe.
   Das ist im Captive Portal robuster und verhindert, dass bereits beim
   Aufruf der lokalen Geraeteseite Daten an Dritte gesendet werden. */

#ifndef BM24_WEB_UI_H
#define BM24_WEB_UI_H

#define BM24_WEB_STYLE \
"*{box-sizing:border-box}" \
":root{color-scheme:dark;--bg:#050607;--panel:#0b0e10;--panel2:#101419;" \
"--line:#2b3137;--line-hot:#6c3b00;--orange:#ff8a00;--orange2:#ffb000;" \
"--cyan:#00b7e8;--text:#f4f5f6;--muted:#929ca6;--ok:#42d392;" \
"--danger:#ff675f;--shadow:0 18px 60px rgba(0,0,0,.38)}" \
"html{min-height:100%;background:var(--bg)}" \
"body{min-height:100vh;margin:0;overflow-x:hidden;font:15px/1.55 Inter,ui-sans-serif,system-ui," \
"-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;color:var(--text);" \
"background:radial-gradient(circle at 82% -10%,rgba(255,138,0,.13)," \
"transparent 32rem),radial-gradient(circle at -10% 35%,rgba(0,183,232,.07)," \
"transparent 28rem),var(--bg)}" \
"body:before{content:\"\";position:fixed;inset:0;pointer-events:none;opacity:.2;" \
"background-image:linear-gradient(rgba(255,255,255,.018) 1px,transparent 1px)," \
"linear-gradient(90deg,rgba(255,255,255,.018) 1px,transparent 1px);" \
"background-size:32px 32px;mask-image:linear-gradient(to bottom,#000,transparent 80%)}" \
"a{color:inherit}.wrap{width:min(1080px,calc(100% - 32px));margin:auto}" \
".topbar{height:76px;display:flex;align-items:center;justify-content:space-between;" \
"gap:18px;border-bottom:1px solid rgba(255,255,255,.08)}" \
".brand{display:inline-flex;align-items:center;gap:11px;text-decoration:none;" \
"font-weight:900;letter-spacing:.055em;font-size:20px}" \
".brand-mark{display:grid;place-items:center;width:38px;height:38px;color:#070707;" \
"font-size:14px;letter-spacing:-.04em;background:linear-gradient(135deg," \
"var(--orange2),var(--orange));clip-path:polygon(0 0,28px 0,38px 10px," \
"38px 38px,10px 38px,0 28px)}" \
".brand strong{color:var(--orange)}.brand sup{font-size:9px;color:var(--muted);" \
"margin-left:2px}.top-actions{display:flex;align-items:center;gap:10px;flex-wrap:wrap}" \
".eyebrow{margin:0 0 8px;color:var(--orange);font-size:11px;font-weight:800;" \
"letter-spacing:.16em;text-transform:uppercase}" \
".chip,.state{display:inline-flex;align-items:center;gap:7px;padding:7px 10px;" \
"border:1px solid var(--line);border-radius:999px;background:rgba(10,13,16,.78);" \
"color:var(--muted);font-size:11px;font-weight:800;letter-spacing:.07em;" \
"text-transform:uppercase}.state:before{content:\"\";width:7px;height:7px;" \
"border-radius:50%;background:var(--muted);box-shadow:0 0 12px currentColor}" \
".state.ok{color:var(--ok);border-color:rgba(66,211,146,.32)}" \
".state.ok:before{background:var(--ok)}.state.bad{color:var(--danger);" \
"border-color:rgba(255,103,95,.32)}.state.bad:before{background:var(--danger)}" \
".button{display:inline-flex;align-items:center;justify-content:center;gap:8px;" \
"min-height:44px;padding:0 17px;border:1px solid var(--line-hot);border-radius:7px;" \
"background:linear-gradient(180deg,#17120c,#0d0e10);color:#fff;text-decoration:none;" \
"font:800 12px/1 ui-sans-serif,system-ui;letter-spacing:.06em;text-transform:uppercase;" \
"cursor:pointer;transition:.18s transform,.18s border-color,.18s background}" \
".button:hover{transform:translateY(-1px);border-color:var(--orange)}" \
".button.primary{border:0;background:linear-gradient(135deg,var(--orange2)," \
"var(--orange));color:#080808;box-shadow:0 9px 28px rgba(255,138,0,.18)}" \
".button.ghost{background:rgba(255,255,255,.025);border-color:var(--line)}" \
".panel{position:relative;background:linear-gradient(145deg,rgba(16,20,24,.96)," \
"rgba(8,10,12,.96));border:1px solid var(--line);border-radius:13px;box-shadow:var(--shadow)}" \
".panel:before{content:\"\";position:absolute;left:14px;right:14px;top:-1px;" \
"height:1px;background:linear-gradient(90deg,transparent,var(--orange),transparent);" \
"opacity:.55}.section-head{display:flex;align-items:end;justify-content:space-between;" \
"gap:16px;margin:34px 0 14px}.section-head h2{margin:0;font-size:24px;line-height:1.15}" \
".section-head p{margin:4px 0 0;color:var(--muted)}" \
".commerce{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}" \
".offer{display:flex;flex-direction:column;min-height:190px;padding:20px;" \
"background:rgba(12,15,18,.9);border:1px solid var(--line);border-radius:11px;" \
"text-decoration:none;transition:.18s transform,.18s border-color}" \
".offer:hover{transform:translateY(-2px);border-color:var(--line-hot)}" \
".offer.featured{background:linear-gradient(145deg,rgba(255,138,0,.12)," \
"rgba(12,15,18,.95));border-color:var(--line-hot)}" \
".offer-tag{color:var(--orange);font-size:10px;font-weight:900;letter-spacing:.14em;" \
"text-transform:uppercase}.offer h3{margin:10px 0 6px;font-size:20px}" \
".offer p{margin:0;color:var(--muted);font-size:13px}.offer-cta{margin-top:auto;" \
"padding-top:18px;color:#fff;font-size:12px;font-weight:850;letter-spacing:.04em}" \
".offer-cta span{color:var(--orange);padding-left:4px}" \
".fineprint{color:var(--muted);font-size:11px}.footer{display:flex;" \
"justify-content:space-between;gap:18px;margin-top:34px;padding:23px 0 34px;" \
"border-top:1px solid rgba(255,255,255,.07);color:var(--muted);font-size:12px}" \
".footer a{color:#d7dce0;text-decoration:none}.footer a:hover{color:var(--orange)}" \
"@media(max-width:760px){.commerce{grid-template-columns:1fr}.topbar{height:auto;" \
"padding:16px 0}.top-actions .chip{display:none}.section-head{align-items:start;" \
"flex-direction:column}.footer{flex-direction:column}.wrap{width:min(calc(100% - 22px),1080px)}}" \
"@media(max-width:480px){.topbar{align-items:flex-start;flex-wrap:wrap}.top-actions{width:100%;" \
"justify-content:space-between}.brand{font-size:17px}.brand-mark{width:34px;height:34px}" \
".button{padding:0 13px}.top-actions{gap:6px}}"

#define BM24_WEB_BRAND \
"<a class=brand href=/><span class=brand-mark>24</span><span><strong>BIT</strong>" \
"MINER24<sup>2.0</sup></span></a>"

#define BM24_SHOP_NERDNOS \
"https://bitminer24.de/products/nerdnos-highspeed-nerdminer"

#define BM24_SHOP_V2 \
"https://bitminer24.de/products/nerdminer-v2-kaufen"

#define BM24_SHOP_HARDWARE \
"https://bitminer24.de/collections/hardware"

#define BM24_GUIDES \
"https://bitminer24.de/pages/wissen"

#define BM24_SUPPORT \
"https://bitminer24.de/pages/contact"

#define BM24_WEB_UPDATER \
"https://bitminer24.de/pages/firmware-update"

#endif /* BM24_WEB_UI_H */
