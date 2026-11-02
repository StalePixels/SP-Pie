#!/bin/bash
# Simulate the SP-Pie CPLD logic with nvc and run the self-checking testbench.
# Runs the testbench once for each queue depth given (default: 2, 4 and 8).
# Exit status is nonzero if any run fails.
#
#   ./sim.sh          # DEPTH 2, 4 and 8
#   ./sim.sh 4        # DEPTH 4 only
#
# Needs nvc (macOS: brew install nvc). Works with macOS /bin/bash 3.2.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
work="$here/build"

if ! command -v nvc >/dev/null 2>&1; then
  echo "sim.sh: nvc not found (brew install nvc)" >&2
  exit 2
fi

depths="$*"
if [ -z "$depths" ]; then
  depths="2 4 8"
fi

mkdir -p "$work"
cd "$work"

nvc --std=2008 -a "$here/sp_pie_cpld.vhd" "$here/tb/tb_sp_pie_cpld.vhd"

status=0
for depth in $depths; do
  echo "== DEPTH $depth"
  if ! nvc --std=2008 -e -gDEPTH="$depth" tb_sp_pie_cpld -r --exit-severity=error; then
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "sim.sh: all runs passed"
else
  echo "sim.sh: FAILED" >&2
fi
exit "$status"
