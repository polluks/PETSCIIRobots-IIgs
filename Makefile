# Makefile for the Apple IIgs intro-screen loader (vbcc, 65816 backend)
#
# Builds a Super Hi-Res intro-screen loader for the Apple IIgs using the
# shadowed-memory fast refresh technique (see shr.s / shr.c).
#
# Requirements:
#   - vbcc with the 65816 backend and the "iigs" target configuration.
#   - vasm 6502 oldstyle flavour (for shr.s).
#   - Point VBCC at the vbcc root (export VBCC=...).

VBCC    ?= /root/ai/vbcc6809/vbcc6809_linux/vbcc
VC      = $(VBCC)/bin/vc
VASM    = $(VBCC)/bin/vasm6502_oldstyle
TARGET  = +iigs

ASFLAGS = -816 -vobj3 -quiet -nowarn=62 -opt-branch -ldots -Fvobj
CFLAGS  = -O2

# AppleCommander jar used to build the ProDOS disk image (make dsk).
AC_JAR  ?= AppleCommander-ac-14.0.jar

OBJS = main.o shr.o shr_asm.o

all: intro

# Build the 800K ProDOS disk (PETSCIROB) containing the INTRO executable.
dsk: intro
	java -jar $(AC_JAR) -pro800 petsciirobots.dsk PETSCIROB
	java -jar $(AC_JAR) -p petsciirobots.dsk INTRO EXE 2000 < intro

# Convert the PNG into screen.bin and image_data.h
image_data.h: convert_png.py introscreen.png
	python3 convert_png.py

# Convert KickAssembler sources to vasm, then assemble the PET game.
# PETROBOTS12.s includes BACKGROUND_TASKS.s.
petrobots: PETROBOTS12.s BACKGROUND_TASKS.s
	$(VASM) -Fbin -dotdir PETROBOTS12.s -o petrobots.bin

PETROBOTS12.s BACKGROUND_TASKS.s: convert.sh convert_kick_vasm.py
	./convert.sh

main.o: main.c shr.h image_data.h
	$(VC) $(TARGET) $(CFLAGS) -c main.c -o $@

shr.o: shr.c shr.h
	$(VC) $(TARGET) $(CFLAGS) -c shr.c -o $@

shr_asm.o: shr.s
	$(VASM) $(ASFLAGS) $< -o $@

intro: $(OBJS)
	$(VC) $(TARGET) -o $@ $(OBJS)

clean:
	rm -f *.o intro intro.map mapfile petrobots.bin

.PHONY: all clean dsk petrobots
