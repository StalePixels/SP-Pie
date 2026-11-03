#!/bin/bash
# Run the demo tape headless in MAME and press its keys. Usage:
#   run.sh MACHINE OUTDIR [START]
# MACHINE is spec128 or specpls3. OUTDIR receives the screenshots and the
# manifest. MAME comes from the scratchpad build described in
# tests/sound/README.md; set MAME_DIR to use another.
set -e
here=$(cd "$(dirname "$0")" && pwd)
machine=${1:?machine}; out=$(cd "$(dirname "${2:?outdir}")" && pwd)/$(basename "$2")
mame_dir=${MAME_DIR:-/u/GIT/Hardware/TK-Pie/scratchpad/mame}
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
rm -rf "$out"; mkdir -p "$out"
cd "$mame_dir"
extra=""
[ "$machine" = spec128 ] && extra="-ay_slot ay_turbosound"
START=${3:-240} MANIFEST=$out/manifest.txt TYPE_TEXT='\r' timeout 300 ./mame-sinclair "$machine" \
    -rompath "$PWD/roms" $extra -cass "$here/../build/demo.tap" \
    -video none -sound none -nothrottle -skip_gameinfo \
    -autoboot_script "$here/demo_keys.lua" -seconds_to_run $(( ${3:-240} + 24 )) \
    -snapshot_directory "$out" -snapname %i > "$out/mame.log" 2>&1
cat "$out/manifest.txt"
