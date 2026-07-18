#!/usr/bin/env bash
# Genera/regenera los golden frames de referencia de SUPERNOVA (§9.4, Metal = referencia dorada).
# Requiere GPU Metal REAL (los runners de CI sin Metal NO regeneran; GoldenFrameTest se auto-saltea ahí).
# Uso: tools/supernova-goldens.sh            (genera a plugins/supernova/tests/golden/)
#      OVNI_JUCE_DIR=... tools/supernova-goldens.sh
set -euo pipefail
cd "$(dirname "$0")/.."

JUCE_DIR="${OVNI_JUCE_DIR:-$HOME/PLUGINS/orbita/JUCE}"
cmake --preset dev -DOVNI_JUCE_DIR="$JUCE_DIR" >/dev/null
cmake --build build --target OvniSupernovaRender >/dev/null
BIN="build/tests/OvniSupernovaRender_artefacts/Release/OvniSupernovaRender"

GOLDEN="plugins/supernova/tests/golden"
# escenario  keyframes (deben coincidir con los Case de GoldenFrameTest.cpp)
emit_for() { case "$1" in
  idle) echo "40";; kick) echo "20,40";; sustained-bass) echo "50";;
  treble-shimmer) echo "40";; rms-breathe) echo "30";; explode-param) echo "24";; esac; }

for s in idle kick sustained-bass treble-shimmer rms-breathe explode-param; do
  "$BIN" --scenario "$s" --frames 60 --width 512 --height 512 \
         --emit "$(emit_for "$s")" --out-dir "$GOLDEN/$s"
done
echo "goldens en $GOLDEN/"
