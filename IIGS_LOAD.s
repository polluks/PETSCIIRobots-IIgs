; IIGS_LOAD.s - ProDOS 8 disk loading for PETSCII Robots 12 on the Apple IIgs.
;
; Replaces the PET KERNAL LOAD ($F356) used by TILE_LOAD_ROUTINE and
; MAP_LOAD_ROUTINE.  The game is a flat 6502 program running in emulation
; mode, so the ProDOS 8 MLI at $BF00 is called directly.
;
; The tileset and level files are ProDOS BIN files whose first two bytes are
; the PET load-address header ($5000 / $5D00).  The loader skips that header
; and reads the payload straight to its destination (same layout as the
; Apple III port):
;
;   TILESET   2818 bytes -> skip 2, read $0B00 (2816) bytes to $5000
;   LEVEL.x   8962 bytes -> skip 2, read $2300 (8960) bytes to $5D00
;             ($5D00: 512-byte unit block + 256 filler, $6000: 8K map)
;
; MLI parameter blocks (ProDOS 8 Technical Reference Manual, 4.5):
;   OPEN  $C8: +0 count(3) +1 pathname(2) +3 io_buffer(2) +5 ref_num(1,result)
;   READ  $CA: +0 count(4) +1 ref_num(1) +2 data_buffer(2) +4 request(2)
;              +6 trans_count(2,result)
;   CLOSE $CC: +0 count(1) +1 ref_num(1)

P8_OPEN         = $C8
P8_READ         = $CA
P8_CLOSE        = $CC
MLI             = $BF00

IIGS_IOBUF      = $4C00         ; 1K OPEN io buffer: page-aligned, free RAM

; ---------------------------------------------------------------------------
; PLAT_LOAD_FILE - load one file's payload to LOAD_DST.
;   LOAD_NAME (2) = pointer to a length-prefixed ProDOS pathname
;   LOAD_DST  (2) = destination address
;   LOAD_LEN  (2) = payload bytes to read (after the 2-byte header)
; Returns carry clear on success, set on error.
PLAT_LOAD_FILE:
        ; The VBL interrupt handler shares zero page with ProDOS MLI, so mask
        ; the VBL interrupt (INTEN bit 3) for the duration of the disk I/O.
        ; INTEN is saved and restored exactly, so a load that happens before
        ; SETUP_INTERRUPT (the tileset) does not leave VBL enabled with no
        ; handler installed.  The I flag is left alone, so MLI/disk firmware
        ; may still use interrupts if it needs them.
        LDA $C041
        STA PLF_INTEN
        AND #$F7
        STA $C041

        ; ---------------------------- OPEN ---------------------------------
        LDA LOAD_NAME
        STA IIGS_PB+1
        LDA LOAD_NAME+1
        STA IIGS_PB+2
        LDA #<IIGS_IOBUF
        STA IIGS_PB+3
        LDA #>IIGS_IOBUF
        STA IIGS_PB+4
        JSR IIGS_MLI_OPEN
        BCC PLF_OPENOK
        JSR PLF_INTEN_RESTORE
        SEC                     ; OPEN failed: nothing to close
        RTS
PLF_OPENOK:
        LDA IIGS_PB+5           ; ref_num result
        STA IIGS_PB+1

        ; --------------------- skip the 2-byte header ----------------------
        LDA #<IIGS_HDR
        STA IIGS_PB+2
        LDA #>IIGS_HDR
        STA IIGS_PB+3
        LDA #2
        STA IIGS_PB+4
        LDA #0
        STA IIGS_PB+5
        JSR IIGS_MLI_READ
        BCS PLF_ERR

        ; --------------------- read the payload ----------------------------
PLF_LOOP:
        LDA LOAD_LEN
        ORA LOAD_LEN+1
        BEQ PLF_DONE            ; all bytes read
        LDA LOAD_LEN
        STA IIGS_PB+4           ; request = min(remaining, $1000)
        LDA LOAD_LEN+1
        STA IIGS_PB+5
        CMP #$10
        BCC PLF_CHUNK
        LDA #0
        STA IIGS_PB+4
        LDA #$10
        STA IIGS_PB+5
PLF_CHUNK:
        LDA LOAD_DST
        STA IIGS_PB+2
        LDA LOAD_DST+1
        STA IIGS_PB+3
        JSR IIGS_MLI_READ
        BCS PLF_ERR
        ; LOAD_DST += trans_count
        LDA IIGS_PB+6
        CLC
        ADC LOAD_DST
        STA LOAD_DST
        LDA IIGS_PB+7
        ADC LOAD_DST+1
        STA LOAD_DST+1
        ; LOAD_LEN -= trans_count
        LDA LOAD_LEN
        SEC
        SBC IIGS_PB+6
        STA LOAD_LEN
        LDA LOAD_LEN+1
        SBC IIGS_PB+7
        STA LOAD_LEN+1
        JMP PLF_LOOP
PLF_DONE:
        JSR IIGS_MLI_CLOSE
        JSR PLF_INTEN_RESTORE
        CLC
        RTS
PLF_ERR:
        JSR IIGS_MLI_CLOSE
        JSR PLF_INTEN_RESTORE
        SEC
        RTS

; Restore INTEN to its pre-load value.
PLF_INTEN_RESTORE:
        LDA PLF_INTEN
        STA $C041
        RTS

; --------------------------- MLI dispatchers -------------------------------
IIGS_MLI_OPEN:
        LDA #3                  ; OPEN has 3 parameters
        STA IIGS_PB
        JSR MLI
        dfb P8_OPEN
        da IIGS_PB
        RTS
IIGS_MLI_READ:
        LDA #4                  ; READ has 4 parameters
        STA IIGS_PB
        JSR MLI
        dfb P8_READ
        da IIGS_PB
        RTS
IIGS_MLI_CLOSE:
        LDA #1                  ; CLOSE has 1 parameter
        STA IIGS_PB
        JSR MLI
        dfb P8_CLOSE
        da IIGS_PB
        RTS

; ------------------------------- state -------------------------------------
LOAD_NAME:  dsb 2
LOAD_DST:   dsb 2
LOAD_LEN:   dsb 2
IIGS_PB:    dsb 8
IIGS_HDR:   dsb 2
PLF_INTEN:  dsb 1
