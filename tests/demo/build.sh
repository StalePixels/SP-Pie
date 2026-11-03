#!/bin/bash
# Build the Gigascreen and ULAplus demo.
#   build/demo.tap   tape file for the 128K, +2, +2A and +3
#   build/demo.lst   assembler listing
# Needs sjasmplus. The script looks first at /u/GIT/ZX/bin/sjasmplus, then at
# sjasmplus on the path. Set SJASMPLUS=/path/to/sjasmplus to use another copy.
set -e
here=$(cd "$(dirname "$0")" && pwd)
asm=${SJASMPLUS:-/u/GIT/ZX/bin/sjasmplus}
if [ ! -x "$asm" ]; then
    asm=$(command -v sjasmplus) || { echo "sjasmplus not found" >&2; exit 1; }
fi
cd "$here"
mkdir -p build
rm -f build/demo.tap
"$asm" --nologo -DTAP --lst=build/demo.lst demo.asm
[ -s build/demo.tap ] || { echo "no tape produced" >&2; exit 1; }
ls -l build/demo.tap
