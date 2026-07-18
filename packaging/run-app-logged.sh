#!/bin/bash
# Launch /Applications/SUPERNOVA.app the SAFE way (LaunchServices, stable path)
# while capturing its stdout/stderr to a log — this is how you see the
# [sysaudio] diagnostics WITHOUT the TCC ghost-row trap.
#
# NEVER launch the inner binary (Contents/MacOS/SUPERNOVA) from a terminal and
# NEVER launch from the build tree: both register a PATH-keyed TCC client that
# turns into a ghost row ("exec" icon) in Settings when that path dies.
set -euo pipefail

APP="/Applications/SUPERNOVA.app"
LOG="${SNV_LOG:-/tmp/supernova-run.log}"

[ -d "$APP" ] || { echo "ERROR: $APP not found — run ./packaging/deploy-app.sh first." >&2; exit 1; }

if pgrep -x SUPERNOVA >/dev/null; then
  echo "==> SUPERNOVA already running — quitting it first (stdio can only be captured at launch)"
  pkill -x SUPERNOVA || true
  sleep 1
fi

: > "$LOG"
echo "==> launching $APP  (log: $LOG)"
open "$APP" --stdout "$LOG" --stderr "$LOG"

echo "==> tailing log (Ctrl-C to stop tailing; the app keeps running)"
echo "    look for: '[sysaudio] CAPTURANDO'  →  '[sysaudio] audio fluyendo (…ch @…Hz)'"
tail -f "$LOG"
