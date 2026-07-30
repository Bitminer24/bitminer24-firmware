#!/usr/bin/env bash
# Eine Variante bauen, flashen und messen.
#
# PlatformIO erzeugt sdkconfig.bm24-v2 nur EINMAL und liest sdkconfig.defaults
# danach nicht mehr. Ohne das Loeschen wirkt keine Aenderung an den Vorgaben —
# genau daran ist die Optimierungsstufe vorher stillschweigend gescheitert.
#
#   BM24_PORT=COM4 ./tools/measure.sh "Bezeichnung" [Sekunden]
set -u
LABEL="${1:-unbenannt}"
SECONDS_TO_RUN="${2:-150}"
PORT="${BM24_PORT:?BM24_PORT muss gesetzt sein, z. B. COM4 oder /dev/ttyACM0}"
PIO="${PLATFORMIO:-platformio}"

cd "$(dirname "$0")/.."
export PYTHONIOENCODING=utf-8

rm -f sdkconfig.bm24-v2
"$PIO" run -e bm24-v2 -t upload --upload-port "$PORT" > /tmp/bm24build.log 2>&1
if ! grep -q SUCCESS /tmp/bm24build.log; then
  echo "$LABEL: BUILD/FLASH FEHLGESCHLAGEN"
  grep -iE "error" /tmp/bm24build.log | head -3
  exit 1
fi

python - "$LABEL" "$SECONDS_TO_RUN" "$PORT" <<'PY'
import serial, time, sys, re, statistics
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
label, secs, port = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = serial.Serial(port, 115200, timeout=0.5)
# Anlauf verwerfen: Hochlauf und erste Pool-Verbindung verzerren das Mittel
time.sleep(20)
end = time.time() + secs
buf = b""; rates = []; temps = []; problems = 0
while time.time() < end:
    d = s.read(4096)
    if not d:
        continue
    buf += d
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        t = line.decode("utf-8", "replace").rstrip()
        m = re.search(r"([\d.]+) kH/s \(HW.*?([\d.]+) C", t)
        if m:
            rates.append(float(m.group(1))); temps.append(float(m.group(2)))
        if "rst:" in t or "wdt" in t.lower() or "overflow" in t:
            problems += 1
s.close()
if not rates:
    print(f"{label}: keine Messwerte"); sys.exit(1)
dips = sum(1 for r in rates if r < 250)
print(f"{label}: n={len(rates)} Median={statistics.median(rates):.1f} "
      f"Mittel={sum(rates)/len(rates):.1f} Einbrueche={100*dips/len(rates):.1f}% "
      f"Temp={min(temps):.0f}-{max(temps):.0f}C Stoerungen={problems}")
PY
