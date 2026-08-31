#!/bin/sh
# makedsk.sh - build petsciirobots.dsk from the freshly compiled 'intro'
# binary.
#
# Creates an 800K ProDOS disk image named PETSCIROB containing the IIgs
# executable INTRO. Uses AppleCommander:
#   https://github.com/AppleCommander/AppleCommander/releases
# Point AC_JAR at the downloaded AppleCommander jar.

set -e

AC_JAR="${AC_JAR:-AppleCommander-ac-14.0.jar}"

ls intro >/dev/null 2>&1 || { echo "Run 'make' first to build intro"; exit 1; }
[ -f "$AC_JAR" ] || { echo "AppleCommander jar not found: $AC_JAR"; exit 1; }

rm -f petsciirobots.dsk
java -jar "$AC_JAR" -pro800 petsciirobots.dsk PETSCIROB
java -jar "$AC_JAR" -p petsciirobots.dsk INTRO EXE 2000 < intro
echo "Wrote petsciirobots.dsk:"
java -jar "$AC_JAR" -ll petsciirobots.dsk
