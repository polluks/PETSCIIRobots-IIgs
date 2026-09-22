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
VASM    ?= vasm6502_oldstyle
TARGET  = +iigs

ASFLAGS = -816 -vobj3 -quiet -nowarn=62 -opt-branch -ldots -Fvobj
CFLAGS  = -O2

# AppleCommander (on PATH as `ac`) used to build the ProDOS disk image (make dsk).
AC      ?= ac

# NTP music: player binary + title song copied to the disk as NTPPLAYER and
# TITLE.NTP (NTPMUSIC.s reads these at runtime from /PETSCIIROBOTS).
NTP_SONG ?= ntp/robot attack.ntp

OBJS = main.o shr.o shr_asm.o NTPMUSIC.o

all: intro

# Build the 800K ProDOS disk (PETSCIIROBOTS) containing the INTRO executable,
# the NTP player and title song, and the game data (tileset + levels) loaded
# at runtime by IIGS_LOAD.s (requires 2 MB of RAM on the IIgs).
dsk: intro
	$(AC) -pro800 petsciirobots.dsk PETSCIIROBOTS
	$(AC) -p petsciirobots.dsk INTRO EXE 2000 < intro
	$(AC) -p petsciirobots.dsk NTPPLAYER UNK 0 < ntp/NTPPLAYER.bin
	$(AC) -p petsciirobots.dsk TITLE.NTP UNK 0 < "$(NTP_SONG)"
	$(AC) -p petsciirobots.dsk "GET PSYCHED.NTP" UNK 0 < "ntp/get psyched.ntp"
	$(AC) -p petsciirobots.dsk "LOSE.NTP" UNK 0 < "ntp/lose.ntp"
	$(AC) -p petsciirobots.dsk "METAL HEADS.NTP" UNK 0 < "ntp/metal heads.ntp"
	$(AC) -p petsciirobots.dsk "METALLIC BOP.NTP" UNK 0 < "ntp/metallic bop amiga.ntp"
	$(AC) -p petsciirobots.dsk "ROBOT ATTACK.NTP" UNK 0 < "ntp/robot attack.ntp"
	$(AC) -p petsciirobots.dsk "RUSHIN IN.NTP" UNK 0 < "ntp/rushin in.ntp"
	$(AC) -p petsciirobots.dsk "SOUNDFX.NTP" UNK 0 < "ntp/soundfx.ntp"
	$(AC) -p petsciirobots.dsk "WIN.NTP" UNK 0 < "ntp/win.ntp"
	$(AC) -p petsciirobots.dsk TILESET BIN 0x5000 < deploy/TILESET
	@for l in A B C D E F G H I J K L M N; do \
		$(AC) -p petsciirobots.dsk LEVEL.$$l BIN 0x5D00 < deploy/LEVEL-$$l; \
	done

# Convert the PNG into screen.bin and image_data.h
image_data.h: convert_png.py introscreen.png
	python3 convert_png.py

# Convert KickAssembler sources to vasm, then assemble the PET game.
# PETROBOTS12.s includes BACKGROUND_TASKS.s and IIGS_KEYS.s (which
# supplies READ_KEY, the IIgs $C000/$C010 keyboard reader replacing
# the PET GETIN calls).
petrobots: PETROBOTS12.s BACKGROUND_TASKS.s IIGS_KEYS.s IIGS_LOAD.s
	$(VASM) -Fbin -dotdir PETROBOTS12.s -o petrobots.bin

PETROBOTS12.s: PETROBOTS12.ASM convert_kick_vasm.py
	python3 convert_kick_vasm.py PETROBOTS12.ASM PETROBOTS12.s

BACKGROUND_TASKS.s: BACKGROUND_TASKS.ASM convert_kick_vasm.py
	python3 convert_kick_vasm.py BACKGROUND_TASKS.ASM BACKGROUND_TASKS.s

# Build the MOD->NTP converter (host cc) and convert every MOD in Music/ into ntp/.
ntpconvert: ntpconverter.c
	cc -O2 -Wall ntpconverter.c -o ntpconvert

ntp: ntpconvert
	mkdir -p ntp
	@for f in Music/*; do \
		if [ -f "$$f" ]; then \
			b="$${f#Music/}"; \
			b="$${b#mod.}"; \
			./ntpconvert "$$f" STREAM_ALLOW "ntp/$$b.ntp" || exit 1; \
		fi \
	done

main.o: main.c shr.h image_data.h
	$(VC) $(TARGET) $(CFLAGS) -c main.c -o $@

shr.o: shr.c shr.h
	$(VC) $(TARGET) $(CFLAGS) -c shr.c -o $@

shr_asm.o: shr.s
	$(VASM) $(ASFLAGS) $< -o $@

NTPMUSIC.o: NTPMUSIC.s
	$(VASM) $(ASFLAGS) $< -o $@

intro: $(OBJS)
	$(VC) $(TARGET) -o $@ $(OBJS)

clean:
	rm -f *.o intro intro.map mapfile petrobots.bin

.PHONY: all clean dsk petrobots ntp ntpconvert
