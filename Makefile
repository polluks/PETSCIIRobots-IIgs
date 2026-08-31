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

OBJS = main.o shr.o shr_asm.o

all: intro

# Convert the PNG into screen.bin and image_data.h
image_data.h: convert_png.py introscreen.png
	python3 convert_png.py

main.o: main.c shr.h image_data.h
	$(VC) $(TARGET) $(CFLAGS) -c main.c -o $@

shr.o: shr.c shr.h
	$(VC) $(TARGET) $(CFLAGS) -c shr.c -o $@

shr_asm.o: shr.s
	$(VASM) $(ASFLAGS) $< -o $@

intro: $(OBJS)
	$(VC) $(TARGET) -o $@ $(OBJS)

clean:
	rm -f *.o intro intro.map mapfile

.PHONY: all clean
