#!/bin/sh
# makedsk.sh - build intro.dsk from the freshly compiled 'intro' binary.
#
# Creates an 800K ProDOS disk image named PETSCII containing the IIgs
# executable INTRO. Uses AppleCommander:
#   https://github.com/AppleCommander/AppleCommander/releases
# Point AC_JAR at the downloaded AppleCommander jar.

set -e

AC_JAR="${AC_JAR:-AppleCommander-ac-14.0.jar}"

ls intro >/dev/null 2>&1 || { echo "Run 'make' first to build intro"; exit 1; }
[ -f "$AC_JAR" ] || { echo "AppleCommander jar not found: $AC_JAR"; exit 1; }

rm -f intro.dsk
java -jar "$AC_JAR" -pro800 intro.dsk PETSCII
java -jar "$AC_JAR" -p intro.dsk INTRO EXE 2000 < intro
echo "Wrote intro.dsk:"
java -jar "$AC_JAR" -ll intro.dsk
