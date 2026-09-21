#!/usr/bin/env python3
"""Convert KickAssembler PETROBOTS12.ASM to vasm (6502 oldstyle) syntax.

Translations:
    !to "file",cbm            -> comment (output handled by vasm -o/-Fbin)
    *=$0401                   -> org $0401
    !BYTE v1,v2,...           -> byte  v1,v2,...
    !PET"..."                 -> byte  <petscii bytes>
    !SCR"..."                 -> byte  <c64 screen-code bytes>
    !SOURCE "FILE"            -> include "FILE"
    < and > low/high bytes    -> unchanged (vasm supports them)
    label = expr              -> unchanged (vasm constant symbols)
"""
import os
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "PETROBOTS12.ASM"
DST = sys.argv[2] if len(sys.argv) > 2 else (
    SRC.rsplit('.', 1)[0] + '.s')


def petscii(s: str) -> list:
    """KickAssembler !PET: convert ASCII str to PETSCII bytes."""
    out = []
    for ch in s:
        o = ord(ch)
        if 'a' <= ch <= 'z':
            out.append(65 + (o - 97))        # lowercase -> 65..90
        elif 'A' <= ch <= 'Z':
            out.append(193 + (o - 65))       # uppercase -> 193..218
        else:
            out.append(o)                     # space/digits/punct -> identity
    return out


def screencode(s: str) -> list:
    """KickAssembler !SCR: convert ASCII str to C64 screen-code bytes."""
    out = []
    for ch in s:
        o = ord(ch)
        if 'a' <= ch <= 'z':
            out.append(1 + (o - 97))         # lowercase -> 1..26
        elif 'A' <= ch <= 'Z':
            out.append(65 + (o - 65))        # uppercase -> 65..90
        else:
            out.append(o)                     # space/digits/punct -> identity
    return out


# Regex for the bang directives. Strings may contain escaped quotes.
DIR = re.compile(
    r'(?P<dir>![A-Za-z]+)(?P<rest>.*)', re.IGNORECASE)

# 6502/65C02 mnemonics used to detect column-0 instruction lines.
MNEMONICS = set("""ADC AND ASL BCC BCS BEQ BIT BMI BNE BPL BRA BRK BVC BVS
CLC CLD CLI CLV CMP CPX CPY DEC DEX DEY EOR INC INX INY JMP JSR LDA LDX
LDY LSR NOP ORA PHA PHP PLA PLP ROL ROR RTI RTS SBC SEC SED SEI STA STX
STY TAX TAY TSX TXA TXS TYA""".split())


def encode_string(s: str, kind: str) -> str:
    s = s.replace('\\\\', '\\').replace('\\"', '"')
    func = petscii if kind == 'pet' else screencode
    vals = func(s)
    return ", ".join("$%02X" % v for v in vals)


def translate_data_body(body: str, kind: str) -> str:
    """Convert a !BYTE/!PET/!SCR body (e.g. ` 13,13` or ` "...",13,0`)
    into vasm `byte ...` arguments, expanding any embedded strings."""
    # Tokenize the body: strings and non-strings.
    tokens = []
    i = 0
    while i < len(body):
        if body[i] in '"\'':
            quote = body[i]
            j = i + 1
            while j < len(body) and body[j] != quote:
                if body[j] == '\\':
                    j += 1
                j += 1
            tokens.append(('str', body[i + 1:j], quote))
            i = j + 1
        else:
            j = i
            while j < len(body) and body[j] not in '"\'':
                j += 1
            tokens.append(('raw', body[i:j], None))
            i = j
    parts = []
    for typ, val, quote in tokens:
        if typ == 'raw':
            v = val.strip()
            if v:
                parts.append(v)
        else:
            parts.append(encode_string(val, kind))
    # Normalise separators: raw tokens often carry their own leading comma,
    # and string expansions end with a bare value, so collapse ', ' + ','.
    blob = ",".join(parts)
    # split on commas, drop empties, re-join cleanly
    cleaned = [p.strip() for p in blob.split(",") if p.strip()]
    return ", ".join(cleaned)


def convert_line(line: str) -> str:
    # Indent column-0 lines whose first token is a 6502 mnemonic (an
    # instruction with no leading label). vasm oldstyle would otherwise
    # try to parse the mnemonic as a label.
    m0 = re.match(r'^([A-Z]{1,3})(\s+\S)', line)
    if m0 and m0.group(1) in MNEMONICS:
        line = '\t' + line
    # The IIgs runs under ProDOS 8; the PET KERNAL GETIN ($FFE4) does not
    # exist there. Route keyboard reads through the IIgs-specific READ_KEY
    # routine (IIGS_KEYS.s) which reads the $C000/$C010 soft-switches and
    # translates the resulting ASCII to the PETSCII codes the game checks.
    line = re.sub(r'JSR\s+\$FFE4', 'JSR READ_KEY', line, flags=re.IGNORECASE)
    # Skip pure comments already handled elsewhere; this handles code lines.
    # Remove the '!to' output directive line entirely.
    if re.match(r'\s*![Tt][Oo]\b', line):
        return '; converted: removed !to output directive\n'
    if re.match(r'\s*\*=\$0401', line):
        return line  # *=$0401 is valid vasm 6502 oldstyle syntax
    # !source -> include (indented so vasm doesn't treat it as a label/directive)
    m = re.match(r'^(\s*)(?:label:)?\s*!source\s+"([^"]+)"\s*(;.*)?$', line, re.IGNORECASE)
    if m:
        fname = m.group(2).replace('.ASM', '.s')
        return f'  include "{fname}"{m.group(3) or ""}\n'
    # !byte / !word / !pet / !scr ... -- find the bang directive anywhere after label
    m = re.match(r'^(?P<lead>\s*)(?P<label>[\w.]+)?\s*(?P<colon>:?\s*)?'
                 r'(?P<dir>![A-Za-z]+)\s*(?P<body>.*)$', line, re.IGNORECASE)
    if not m:
        return line
    lead, label, colon, dirname, body = (m.group(k) or '' for k in
                                         ('lead', 'label', 'colon', 'dir', 'body'))
    d = dirname.lstrip('!').lower()
    if d == 'to':
        return '; converted: removed !to output directive\n'
    if d == 'source':
        # already handled above; fallback
        return re.sub(r'!source\s+"([^"]+)"', r'include "\1"', line, flags=re.IGNORECASE)
    if d in ('byte', 'word'):
        vasm = 'byte' if d == 'byte' else 'word'
        # preserve any raw numeric args as-is
        inside = body.strip()
        if not inside:
            arg = ''
        else:
            # strip trailing comment
            mc = re.match(r'^(.*?)(\s*;.*)?$', inside)
            datapart = mc.group(1).strip()
            comment = mc.group(2) or ''
            arg = translate_data_body(datapart, 'byte')
            return f'{lead}{label}{colon} {vasm} {arg}{comment}\n'
        return f'{lead}{label}{colon} {vasm}\n'
    if d in ('pet', 'scr'):
        inside = body.strip()
        mc = re.match(r'^(.*?)(\s*;.*)?$', inside)
        datapart = mc.group(1).strip()
        comment = mc.group(2) or ''
        arg = translate_data_body(datapart, d)
        return f'{lead}{label}{colon} byte {arg}{comment}\n'
    return line


# IIgs game port: replacement for the two PET KERNAL LOAD routines.  They now
# stage LOAD_NAME / LOAD_DST / LOAD_LEN for IIGS_LOAD.s (PLAT_LOAD_FILE), which
# loads the ProDOS files TILESET / LEVEL.x straight into game RAM.
TILE_LOAD_BODY = (
    'TILE_LOAD_ROUTINE:\t\n'
    '\tLDA\t#<TILENAME\n'
    '\tSTA\tLOAD_NAME\n'
    '\tLDA\t#>TILENAME\n'
    '\tSTA\tLOAD_NAME+1\n'
    '\tLDA\t#$00\n'
    '\tSTA\tLOAD_DST\n'
    '\tLDA\t#$50\n'
    '\tSTA\tLOAD_DST+1\t;tileset payload -> $5000\n'
    '\tLDA\t#$00\n'
    '\tSTA\tLOAD_LEN\n'
    '\tLDA\t#$0B\n'
    '\tSTA\tLOAD_LEN+1\t;2816 bytes (DESTRUCT_PATH + 10 tile tables)\n'
    '\tJMP\tPLAT_LOAD_FILE\n'
)
MAP_LOAD_BODY = (
    'MAP_LOAD_ROUTINE:\t\n'
    '\tLDA\t#<MAPNAME\n'
    '\tSTA\tLOAD_NAME\n'
    '\tLDA\t#>MAPNAME\n'
    '\tSTA\tLOAD_NAME+1\n'
    '\tLDA\t#$00\n'
    '\tSTA\tLOAD_DST\n'
    '\tLDA\t#$5D\n'
    '\tSTA\tLOAD_DST+1\t;unit block + filler -> $5D00\n'
    '\tLDA\t#$00\n'
    '\tSTA\tLOAD_LEN\n'
    '\tLDA\t#$23\n'
    '\tSTA\tLOAD_LEN+1\t;8960 bytes ($0300 units/spare + $2000 map at $6000)\n'
    '\tJMP\tPLAT_LOAD_FILE\n'
)

# IIgs 60 Hz VBL interrupt.  RUNIRQ is installed as the ProDOS user interrupt
# vector at $03FE/$03FF (bank $00), which the IIgs firmware calls after its own
# interrupt handler.  The firmware enters that handler with DBR=$00, DP=$00,
# 8-bit registers, in native mode, and the handler returns with RTI.  Bit 3 of
# INTEN ($C041) enables the VBL interrupt; a write to CLRVBLINT ($C047) clears
# it.  (Firmware Reference ch.8; Hardware Reference VGC/INTEN registers.)
SETUP_INTERRUPT_BODY = (
    'SETUP_INTERRUPT:\n'
    '\tSEI\t\t\t; Disable interrupts while installing the handler\n'
    '\tLDA\t#<RUNIRQ\t; point the ProDOS user IRQ vector at RUNIRQ\n'
    '\tSTA\t$03FE\t\t; ($03FE/$03FF, bank $00)\n'
    '\tLDA\t#>RUNIRQ\n'
    '\tSTA\t$03FF\n'
    '\tLDA\t$C041\t\t; INTEN: enable VBL interrupt (bit 3)\n'
    '\tORA\t#$08\n'
    '\tSTA\t$C041\n'
    '\tLDA\t#$00\n'
    '\tSTA\t$C047\t\t; clear any pending VBL interrupt\n'
    '\tCLI\t\t\t; Reenable interrupts\n'
    '\tRTS\n'
)

IRQ31_BODY = (
    'IRQ31:\tLDA\t#$00\n'
    '\tSTA\t$C047\t\t; clear the VBL interrupt flag\n'
    '\tRTI\t\t\t; back to the firmware interrupt handler\n'
)


def apply_iigs_game_patches(text: str) -> str:
    """Apply the IIgs game-port rewrites to the converted PETROBOTS12.s.

    Kept here (rather than hand-edited into the .s) so that regenerating the
    .s from the original KickAssembler source preserves the port.
    """
    def sub_once(pattern: str, repl: str, what: str, flags: int = 0) -> None:
        nonlocal text
        new, n = re.subn(pattern, repl, text, flags=flags)
        if n != 1:
            raise SystemExit(
                f"convert_kick_vasm.py: expected exactly 1 {what} to patch, found {n}")
        text = new

    # 1. PET filenames -> ProDOS pathnames (length-prefixed).  The level letter
    #    is the last byte of MAPNAME and is patched at runtime.
    sub_once(
        r'^TILENAME[ \t]+byte[^\n]*\n^MAPNAME[ \t]+byte[^\n]*\n',
        '; ProDOS pathnames (length-prefixed). MAPNAME\'s last byte is the level\n'
        '; letter, patched by DISPLAY_MAP_NAME (ProDOS names cannot contain \'-\').\n'
        'TILENAME byte 22, "/PETSCIIROBOTS/TILESET"\n'
        'MAPNAME byte 22, "/PETSCIIROBOTS/LEVEL.A"\n',
        'TILENAME/MAPNAME', flags=re.MULTILINE)

    # 2. Do not patch the PET LOAD vector ($F356 -> $F322) from
    #    DETECT_ROM_VERSION; the IIgs uses IIGS_LOAD.s instead.
    sub_once(
        r'^\tLDA\t#\$22\n\tSTA\tLDR1\+1\n\tSTA\tLDR2\+1\n',
        ';\tLDA\t#$22\t;PET BASIC ROM: patch LOAD vector ($F356->$F322)\n'
        ';\tSTA\tLDR1+1\t;not used on the IIgs (disk loading is in IIGS_LOAD.s)\n'
        ';\tSTA\tLDR2+1\n',
        'DETECT_ROM_VERSION LOAD patch', flags=re.MULTILINE)

    # 3/4. Tileset and level loaders -> PLAT_LOAD_FILE.
    sub_once(
        r'^TILE_LOAD_ROUTINE:[^\n]*\n(?:\t[^\n]*\n)*?LDR1:\tJSR\t\$F356[^\n]*\n\tRTS\n',
        TILE_LOAD_BODY, 'TILE_LOAD_ROUTINE', flags=re.MULTILINE)
    sub_once(
        r'^MAP_LOAD_ROUTINE:[^\n]*\n(?:\t[^\n]*\n)*?LDR2:\tJSR\t\$F356[^\n]*\n\tRTS\n',
        MAP_LOAD_BODY, 'MAP_LOAD_ROUTINE', flags=re.MULTILINE)

    # 5. The level letter is the last byte of the ProDOS pathname.
    sub_once(r'\tSTA\tMAPNAME\+6\b',
             '\tSTA\tMAPNAME+22\t;last char of "/PETSCIIROBOTS/LEVEL.A"',
             'MAPNAME+6 patch')

    # 6. PET IRQ hookup -> IIgs user interrupt vector ($03FE/$03FF) + VBL.
    sub_once(
        r'^SETUP_INTERRUPT:\n(?:\t[^\n]*\n){11}',
        SETUP_INTERRUPT_BODY, 'SETUP_INTERRUPT', flags=re.MULTILINE)

    # 7. Return from the handler through the firmware instead of the PET ROM.
    sub_once(
        r'^IRQ31:\tJMP\t\$E455[^\n]*\n',
        IRQ31_BODY, 'IRQ31 return', flags=re.MULTILINE)

    # 8. The firmware enters the handler in native mode; force 8-bit A/X so the
    #    6502 body executes correctly.  Assembled as raw opcode bytes because
    #    the game is assembled for the plain 6502 (enabling -816 would make the
    #    `da` directives in IIGS_LOAD.s emit 3-byte addresses and break MLI).
    sub_once(
        r'^RUNIRQ:\n',
        'RUNIRQ:\n'
        '\tdfb\t$E2, $30\t\t; SEP #$30 (select 8-bit A/X in native mode)\n',
        'RUNIRQ entry', flags=re.MULTILINE)

    return text


def main():
    with open(SRC) as f:
        raw = f.read()
    lines = raw.splitlines()
    out = []
    # add a header comment (strip CR from each line via splitlines already)
    out.append('; PETSCII Robots 12 - converted from KickAssembler to vasm (6502 oldstyle)')
    out.append('; Source: PETROBOTS12.ASM by David Murray')
    out.append('')
    for line in lines:
        stripped = line.lstrip()
        if stripped.startswith(';'):
            out.append(line)
            continue
        out.append(convert_line(line + '\n').rstrip('\n'))
    # The IIgs keyboard reader lives in IIGS_KEYS.s and the disk loader in
    # IIGS_LOAD.s. Pull them into the top-level output (PETROBOTS12.s);
    # BACKGROUND_TASKS.s is already included by it, so it must not also
    # include this file (READ_KEY would be defined twice).
    text = '\n'.join(out)
    if os.path.basename(DST) == 'PETROBOTS12.s':
        text = apply_iigs_game_patches(text)
        out = text.split('\n')
        out.append('')
        out.append('  include "IIGS_KEYS.s"')
        out.append('  include "IIGS_LOAD.s"')
    with open(DST, 'w') as f:
        f.write('\n'.join(out) + '\n')
    print(f"wrote {DST}")


if __name__ == '__main__':
    main()
