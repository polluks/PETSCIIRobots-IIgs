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
; codesets), so they pass through unchanged. The differences that matter:
;   - arrow keys -> the PETSCII cursor codes the game tests for ($11/$91/...)
;   - ESC -> PET RUN/STOP ($03), for Pause (Ctrl+C still works too)
;   - TAB -> PET HOME ($13), to display the map
;
; READ_KEY also stores the key it returns in LSTX ($0097), the PET KERNAL
; "last key" latch. The game's KEY_REPEAT routine polls $0097 to tell a fresh
; keypress (or keyboard auto-repeat) from a key that is merely held down, and
; uses that to drive fast movement; without this, held keys never repeat.
; Like the PET GETIN it replaces, READ_KEY preserves X and Y.

READ_KEY:
        txa
        pha                     ; preserve X (the PET GETIN does)
        tya
        pha                     ; preserve Y
        lda     $C000           ; keyboard data
        bpl     RK_ZERO         ; bit 7 clear => no key down
        bit     $C010           ; clear the keyboard strobe
        and     #$7F            ; 7-bit ASCII
        ldx     #0
RK_S1:
        cmp     RKT_ASCII,x     ; scan the ASCII->PETSCII translation table
        bne     RK_S2
        lda     RKT_PETSCII,x   ; match: emit the PETSCII code
        jmp     RK_DONE
RK_S2:
        inx
        cpx     #RKT_PETSCII-RKT_ASCII
        bne     RK_S1
        ; no match: A holds the ASCII code, which is the PETSCII code here
RK_DONE:
        sta     $0097           ; LSTX: last key, for KEY_REPEAT
        jmp     RK_EXIT
RK_ZERO:
        lda     #0
RK_EXIT:
        sta     RK_TMP
        pla
        tay                     ; restore Y
        pla
        tax                     ; restore X
        lda     RK_TMP
        rts

RK_TMP: byte    0

; Translation table: ASCII -> PETSCII, one entry per index.
RKT_ASCII:
        byte    $08             ; left arrow  (BS)  -> $9D cursor left
        byte    $15             ; right arrow (NAK) -> $1D cursor right
        byte    $0A             ; down arrow  (LF)  -> $11 cursor down
        byte    $0B             ; up arrow    (VT)  -> $91 cursor up
        byte    $1B             ; ESC               -> $03 RUN/STOP (pause)
        byte    $09             ; TAB               -> $13 HOME (map)
        byte    $0C             ; Clear/Ctrl-L      -> $13 HOME (map)
RKT_PETSCII:
        byte    $9D, $1D, $11, $91, $03, $13, $13
