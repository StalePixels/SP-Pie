#!/bin/bash
# Compile a Quartus project directory with the Linux Quartus install, run in an
# amd64 Docker container under OrbStack (Rosetta). Works with /bin/bash 3.2.
#
# Usage: quartus.sh <project-dir> [revision]
#   revision defaults to the name of the only .qpf file in the directory.
#
# Environment:
#   QUARTUS_HOME   Host path of the Quartus install (the directory that holds
#                  the "quartus" folder). Default: /u/toolchains/quartus
#   QUARTUS_IMAGE  Container image. Default: ubuntu:22.04
set -eu

QUARTUS_HOME="${QUARTUS_HOME:-/u/toolchains/quartus}"
QUARTUS_IMAGE="${QUARTUS_IMAGE:-ubuntu:22.04}"

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 <project-dir> [revision]" >&2
    exit 2
fi
if [ ! -d "$1" ]; then
    echo "error: not a directory: $1" >&2
    exit 2
fi
PROJECT_DIR="$(cd "$1" && pwd -P)"

if [ ! -x "$QUARTUS_HOME/quartus/bin/quartus_sh" ]; then
    echo "error: $QUARTUS_HOME/quartus/bin/quartus_sh not found; set QUARTUS_HOME" >&2
    exit 2
fi

if [ $# -eq 2 ]; then
    REVISION="$2"
else
    set -- "$PROJECT_DIR"/*.qpf
    if [ $# -ne 1 ] || [ ! -f "$1" ]; then
        echo "error: need exactly one .qpf in $PROJECT_DIR, or pass a revision" >&2
        exit 2
    fi
    REVISION="$(basename "$1" .qpf)"
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker (OrbStack) not found in PATH" >&2
    exit 2
fi

# The project is mounted at the same path inside the container so that
# messages from Quartus name host-valid paths. The install is read-only.
exec docker run --rm --platform linux/amd64 \
    -v "$QUARTUS_HOME":/quartus-home:ro \
    -v "$PROJECT_DIR":"$PROJECT_DIR" \
    -w "$PROJECT_DIR" \
    -e HOME=/tmp \
    "$QUARTUS_IMAGE" \
    /quartus-home/quartus/bin/quartus_sh --flow compile "$REVISION"
