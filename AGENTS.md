# AGENTS.md

## Project
Apple IIgs port of *Attack of the PETSCII Robots*. It builds:
- an SHR shadowed-memory intro-screen loader (vbcc 65816, `shr.s`/`shr.c`/`main.c`),
- a vasm 6502 port of the PET game (`PETROBOTS12.s`),
- a host C port of Ninjaforce's MOD->NTP converter (`ntpconverter.c`) that turns
  the Amiga MOD soundtrack in `Music/` into `.ntp` songs for the planned
  NinjaTrackerPlus playback.

## Build targets (Makefile)
- `make` — intro loader. Requires vbcc 65816 (`+iigs`); point `VBCC` at the vbcc
  root. Generates `image_data.h` from `introscreen.png` via `convert_png.py`.
- `make petrobots` — converts KickAssembler sources with `convert_kick_vasm.py`,
  then assembles with vasm 6502 oldstyle.
- `make dsk` — builds `petsciirobots.dsk` (800K ProDOS volume `PETSCIROB`) with
  AppleCommander (`AC_JAR`).
- `make ntp` — builds `ntpconvert` (host `cc`) and converts every MOD in `Music/`
  to `ntp/<name>.ntp`, dropping the leading `mod.` from output names.

## MOD -> NTP converter (`ntpconverter.c`)
- Direct C port of `ntpconverter_lib.php` / `ntpconverter.php` (Ninjaforce NTP
  sources).
- Usage: `./ntpconvert MODFILE [STREAM_FORBIDDEN|STREAM_ALLOW|STREAM_ENFORCE] [OUTFILE]`.
  Default output: MOD filename with a leading `mod.` stripped and `.ntp` appended.
  Errors -> stderr `UNABLE TO CONVERT:` + exit 255; info -> stdout.
- `Music/` MODs are 4-channel ProTracker-layout files with **no extension**
  (iterate `Music/*` and test with `[ -f "$f" ]`, never `Music/*.mod`). Seven
  carry the `!PM!` tag at offset 1080 (already in `MOD_TYPE_TABLE`); `mod.soundfx`
  is `M.K.` with zero instruments.
- Conversion facts that must hold byte-exact:
  - Ensoniq conversion: `(b + 128) & 255`, 0 -> 1.
  - `+8` stopper zeros are appended unless the length is in
    `{256, 512, 1024, 2048, 4096, 8192, 16384, 32768}`.
  - Streamed-loop (type 9) data = head + (loop grown to >= 512) + first 256 bytes
    of the loop.
- Output must be byte-for-byte deterministic across runs; `cc -O2 -Wall` must be
  warning-clean.

## Gotcha: self-append buffer bug
Never append a Buf to itself via `buf_mem(x, x->b, n)` — `buf_grow()` can
`realloc()` and move the buffer, leaving a stale source pointer that reads freed
memory (nondeterministic heap garbage in output). Snapshot the source into a
stack buffer before appending. This bit us for streamed-loop instruments whose
loop length crosses the 4096->8192 capacity boundary (e.g. `minhit`, rl=3856).

## Conventions
- Do not add code comments unless asked.
- Only commit/push when explicitly asked; inspect `git status`/`git diff` first.
- Generated artifacts are committed in this repo by convention
  (`screen.bin`, `petsciirobots.dsk`, `ntp/*.ntp`); the `ntpconvert` binary is
  not.