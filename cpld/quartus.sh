#!/bin/bash
# Compile a Quartus project directory with the Linux Quartus install, run in an
# amd64 Docker container under OrbStack (Rosetta). Works with /bin/bash 3.2.
#
# Usage: quartus.sh <project-dir> [revision]
#   revision defaults to the name of the only .qpf file in the directory.
#
# Environment:
#   QUARTUS_HOME   Host path of the Quartus install (the directory that holds
#                  the "quartus" folder). Required.
#   QUARTUS_IMAGE  Container image name. Default: quartus13-env. If it does not
#                  exist, it is built from ubuntu:22.04 plus the 32-bit and
#                  64-bit runtime libraries Quartus 13.0sp1 needs.
set -eu

QUARTUS_HOME="${QUARTUS_HOME:?set QUARTUS_HOME to the Quartus install}"
QUARTUS_IMAGE="${QUARTUS_IMAGE:-quartus13-env}"

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

if ! docker image inspect "$QUARTUS_IMAGE" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$QUARTUS_IMAGE" - <<'DOCKERFILE'
FROM ubuntu:22.04
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libc6:i386 libstdc++6:i386 libx11-6:i386 libxext6:i386 libxft2:i386 libfreetype6:i386 libsm6:i386 libxrender1:i386 libfontconfig1:i386 libxi6:i386 libxtst6:i386 libglib2.0-0:i386 libzstd1:i386 \
    libstdc++6 libx11-6 libxext6 libxft2 libfreetype6 libsm6 libxrender1 libfontconfig1 libtinfo5 libncurses5 libpng16-16 locales ca-certificates \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
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
