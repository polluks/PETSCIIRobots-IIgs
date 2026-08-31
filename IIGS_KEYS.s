; IIGS_KEYS.s - Apple IIgs keyboard input for PETSCII Robots 12 (6502)
;
; Replaces the PET KERNAL GETIN ($FFE4) calls with direct reads of the
; Apple IIgs / Apple II keyboard soft-switches:
;   $C000 - keyboard data (bit 7 set while a key is being held)
;   $C010 - clear the keyboard strobe
;
; The PET code (PETROBOTS12.s) calls READ_KEY wherever it used to call
; GETIN. READ_KEY returns the translated key code in A (0 if no key is
; pending), matching the GETIN contract the game's CMP checks rely on.
;
; The IIgs keyboard returns *ASCII*; most printable keys already match the
; uppercase PETSCII codes the game compares against (e.g. 'W' = $57 in both
; codesets), so they pass through unchanged. The only differences that matter
; are the arrow keys, which are mapped to the PETSCII cursor codes below.

READ_KEY:
        lda     $C000           ; keyboard data
        bpl     RK_NONE         ; bit 7 clear => no key down
        bit     $C010           ; clear the keyboard strobe
        and     #$7F            ; 7-bit ASCII
        ldx     #0
RK_S1:
        cmp     RKT_ASCII,x     ; scan the ASCII->PETSCII translation table
        bne     RK_S2
        lda     RKT_PETSCII,x   ; match: emit the PETSCII code
        rts
RK_S2:
        inx
        cpx     #RKT_PETSCII-RKT_ASCII
        bne     RK_S1
        rts                     ; no match: pass ASCII through (== PETSCII)

RK_NONE:
        lda     #0
        rts

; Translation table: ASCII -> PETSCII, one entry per index.
; Apple II arrow keys yield ASCII control codes; translate them to the
; PETSCII cursor codes PETSCII Robots 12 compares against ($11/$91/$1D/$9D).
RKT_ASCII:
        byte    $08             ; left arrow  (BS)  -> $9D cursor left
        byte    $15             ; right arrow (NAK) -> $1D cursor right
        byte    $0A             ; down arrow  (LF)  -> $11 cursor down
        byte    $0B             ; up arrow    (VT)  -> $91 cursor up
RKT_PETSCII:
        byte    $9D, $1D, $11, $91
