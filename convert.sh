#!/bin/sh
# convert.sh - convert KickAssembler sources to vasm (6502 oldstyle).
# Produces PETROBOTS12.s and BACKGROUND_TASKS.s (PETROBOTS12.s includes the
# latter). Run from the repo root, or call with an explicit source dir.

set -e

DIR="${1:-$(pwd)}"
PY="$(dirname "$0")/convert_kick_vasm.py"

cd "$DIR"
python3 "$PY" PETROBOTS12.ASM PETROBOTS12.s
python3 "$PY" BACKGROUND_TASKS.ASM BACKGROUND_TASKS.s
