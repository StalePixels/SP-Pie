#!/bin/bash
# Run the .tap tests in a headless MAME and record their sound.
# usage: run_refs.sh OUTDIR
# Environment:
#   MAME      MAME binary with the spectrum, spec128 drivers (default: mame)
#   ROMPATH   MAME rompath holding spectrum.zip and spec128.zip
# MAME has no Spectrum Covox device. The covox run only proves that the
# program loads and finishes; covox_expected.py writes the expected wave.
# MAME never opens a window: video and sound output are off, and the audio
# goes to the WAV file only.
set -e
here=$(cd "$(dirname "$0")" && pwd)
tap=$here/../build/tap
out=${1:?usage: run_refs.sh OUTDIR}
mkdir -p "$out"
out=$(cd "$out" && pwd)
mame=${MAME:-mame}
rompath=${ROMPATH:?set ROMPATH}

export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# run NAME MACHINE SECONDS SNAP_AT TYPE_TEXT [extra mame args]
run() {
    local name=$1 machine=$2 secs=$3 snaps=$4 text=$5
    shift 5
    local tag=$name-$machine
    rm -rf "$out/$tag.snap"
    mkdir -p "$out/$tag.snap"
    TYPE_AT=3 TYPE_TEXT="$text" SNAP_AT="$snaps" timeout 600 "$mame" "$machine" \
        -rompath "$rompath" "$@" -cass "$tap/$name.tap" \
        -video none -sound none -nothrottle -skip_gameinfo \
        -autoboot_script "$here/drive.lua" -seconds_to_run "$secs" \
        -wavwrite "$out/$tag.wav" \
        -snapshot_directory "$out/$tag.snap" -snapname "%i" > "$out/$tag.log" 2>&1
    python3 "$here/analyse.py" "$out/$tag.wav" > "$out/$tag.freq.txt"
}

for t in ay turbosound beeper hud border; do
    run $t spec128 80 70 '\r' -ay_slot ay_turbosound
done
run covox spec128 90 80 '\r' -ay_slot ay_turbosound
run beeper spectrum 60 50 'j""\r'
