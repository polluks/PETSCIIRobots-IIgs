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

# AppleCommander (on PATH as `ac`) used to build the ProDOS disk image (make dist).
AC      ?= ac

# GSSquared IIgs emulator binary, used by "make check".
GSSQUARED ?= /Applications/GSSquared.app/Contents/MacOS/GSSquared

# NTP music: player binary + title song copied to the disk as NTPPLAYER and
# TITLE.NTP (NTPMUSIC.s reads these at runtime from /PETSCIIROBOTS).
# TITLE.NTP is the same file as ROBOT ATTACK.NTP, so only the title copy is
# installed (keeping the duplicate off the disk leaves room for the kernel).
NTP_SONG ?= ntp/robot attack.ntp

# ProDOS 8 kernel (SYS) that boot block 0 loads by name to start the system.
# PRODOS_REF is any ProDOS 8 boot disk with that file (e.g. ProDOS 2.4.x).
PRODOS_REF ?= /Users/sah/g/mii_emu/disks/prodos242.dsk

OBJS = main.o shr.o shr_asm.o NTPMUSIC.o

all: intro

# Build the 800K ProDOS disk image (PETSCIIROBOTS) containing the INTRO
# executable, the NTP player and title song, and the game data (tileset +
# levels) loaded at runtime by IIGS_LOAD.s (requires 2 MB of RAM on the IIgs).
# The image carries the .po extension because GSSquared only treats .dsk/.do
# files as 140K floppies and rejects anything larger; 800K volumes must be
# identified as ProDOS block (.po) media to mount there.
# The intro program is installed as BASIC.SYSTEM (a ProDOS SYS $2000 program)
# so the kernel runs it automatically after boot; nothing opens INTRO by name.
# LEVEL.N is skipped so the bootable PRODOS kernel fits on the volume; the
# map menu still rounds 0-13 down to the 13 installed levels.
dist: intro deploy/PRODOS
	$(AC) -pro800 petsciirobots.po PETSCIIROBOTS
	$(AC) -p petsciirobots.po BASIC.SYSTEM SYS 0x2000 < intro
	$(AC) -p petsciirobots.po PRODOS SYS 0x2000 < deploy/PRODOS
	$(AC) -p petsciirobots.po NTPPLAYER UNK 0 < ntp/NTPPLAYER.bin
	$(AC) -p petsciirobots.po TITLE.NTP UNK 0 < "$(NTP_SONG)"
	$(AC) -p petsciirobots.po "GET PSYCHED.NTP" UNK 0 < "ntp/get psyched.ntp"
	$(AC) -p petsciirobots.po "LOSE.NTP" UNK 0 < "ntp/lose.ntp"
	$(AC) -p petsciirobots.po "METAL HEADS.NTP" UNK 0 < "ntp/metal heads.ntp"
	$(AC) -p petsciirobots.po "METALLIC BOP.NTP" UNK 0 < "ntp/metallic bop amiga.ntp"
	$(AC) -p petsciirobots.po "RUSHIN IN.NTP" UNK 0 < "ntp/rushin in.ntp"
	$(AC) -p petsciirobots.po "SOUNDFX.NTP" UNK 0 < "ntp/soundfx.ntp"
	$(AC) -p petsciirobots.po "WIN.NTP" UNK 0 < "ntp/win.ntp"
	$(AC) -p petsciirobots.po TILESET BIN 0x5000 < deploy/TILESET
	@for l in A B C D E F G H I J K L M; do \
		$(AC) -p petsciirobots.po LEVEL.$$l BIN 0x5D00 < deploy/LEVEL-$$l; \
	done

# Extract the ProDOS 8 kernel from a known-good boot disk (PRODOS_REF).
deploy/PRODOS:
	mkdir -p deploy
	$(AC) -g $(PRODOS_REF) PRODOS $@

# Test: build the disk image, verify it is bootable (PRODOS kernel +
# BASIC.SYSTEM), then launch GSSquared. In the emulator, pick "Apple IIgs",
# click a ProDOS block drive, and select petsciirobots.po.
check: dist
	$(AC) -l petsciirobots.po | grep -q PRODOS && \
	$(AC) -l petsciirobots.po | grep -q BASIC.SYSTEM
	@echo "petsciirobots.po OK: PRODOS + BASIC.SYSTEM present"
	@pgrep -q -f "$(GSSQUARED)" \
		&& echo "GSSquared already running" \
		|| { "$(GSSQUARED)" > /tmp/gs2-check.log 2>&1 & \
			echo "GSSquared started (log: /tmp/gs2-check.log)"; }
	@echo "In GSSquared: pick 'Apple IIgs', drive button, choose petsciirobots.po"

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

.PHONY: all clean dist petrobots ntp ntpconvert check
