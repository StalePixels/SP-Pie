#!/bin/bash
# Build all test programs into build/*.tap. Needs sjasmplus.
# Override the assembler with SJASMPLUS=/path/to/sjasmplus.
set -e
here=$(cd "$(dirname "$0")" && pwd)
asm=${SJASMPLUS:-/u/GIT/ZX/bin/sjasmplus}
if [ ! -x "$asm" ]; then
    asm=$(command -v sjasmplus) || { echo "sjasmplus not found" >&2; exit 1; }
fi
cd "$here"
mkdir -p build
for src in ldir_fill push_clear border_loop ulaplus shadow_flip bank5_c000 \
           plus3_special lock_test; do
    rm -f "build/$src.tap"
    "$asm" --nologo --lst="build/$src.lst" "$src.asm"
    [ -s "build/$src.tap" ] || { echo "no tape produced for $src" >&2; exit 1; }
done
ls -l build/*.tap
