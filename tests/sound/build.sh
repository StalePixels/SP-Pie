#!/bin/bash
# Build the sound test programs.
#   build/tap/NAME.tap   tape file for 48K, 128K, +2 and +3
#   build/dot/NAME       ESXDOS dot command (copy to the BIN folder of the SD card)
# Needs sjasmplus. The script looks first at /u/GIT/ZX/bin/sjasmplus, then at
# sjasmplus on the path. Set SJASMPLUS=/path/to/sjasmplus to use another copy.
set -e
here=$(cd "$(dirname "$0")" && pwd)
asm=${SJASMPLUS:-/u/GIT/ZX/bin/sjasmplus}
if [ ! -x "$asm" ]; then
    asm=$(command -v sjasmplus) || { echo "sjasmplus not found" >&2; exit 1; }
fi
cd "$here"
mkdir -p build/tap build/dot build/lst
for src in ay turbosound beeper covox hud; do
    rm -f "build/tap/$src.tap" "build/dot/$src"
    "$asm" --nologo -DTAP --lst="build/lst/$src-tap.lst" "$src.asm"
    "$asm" --nologo -DDOT --lst="build/lst/$src-dot.lst" "$src.asm"
    [ -s "build/tap/$src.tap" ] || { echo "no tape produced for $src" >&2; exit 1; }
    [ -s "build/dot/$src" ] || { echo "no dot command produced for $src" >&2; exit 1; }
done
rm -f build/dot/spctl
"$asm" --nologo -DDOT --lst="build/lst/spctl-dot.lst" spctl.asm
[ -s build/dot/spctl ] || { echo "no dot command produced for spctl" >&2; exit 1; }
ls -l build/tap/*.tap build/dot/*
