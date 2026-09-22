; shr.s
; Apple IIgs Super Hi-Res loader using shadowed memory.
;
; Motivation (see https://retrocomputing.stackexchange.com/questions/52 ):
;   The SHR display lives in bank $E1, which is serviced by the 1 MHz Mega II
;   chip, so writing it directly is slow.  Instead the caller renders the
;   frame into a FAST back buffer in bank $01.  With SHR shadowing enabled
;   (SHADOW register $C035, bit 3 = 0) every write to bank $01 is mirrored
;   into bank $E1 at full 2.8 MHz speed.
;
;   To push the already-rendered pixels onto the display, this routine copies
;   the back buffer onto itself with the 65816 MVN block-move instruction.
;   The destination is bank $01, so every store is reflected into bank $E1.
;   MVN is a single instruction streaming 32 KB at full speed - far faster
;   than a byte-per-instruction loop poking slow RAM.
;
; The caller must own bank $01 and have SHR shadowing allocated
; (see shr.c / main.c).
;
; vbcc 65816 (IIgs) calling convention:
;   @param A       src   = starting address in the bank-$01 back buffer
;   @param <stack> count = number of bytes to mirror (arg1 in A, arg2 on stack)
;   Callee preserves Y.

	section	"DONTMERGE_text.far.shr_slam.0","acrx"
	a16
	x16
	global	_shr_slam
_shr_slam:
	phy                     ; preserve Y (caller-saved convention)
	pha                     ; stash src (arg1 was in A)
	phb                     ; MVN clobbers the data bank register
	                        ; stack now: DBR(1) src(2,3) Y(4,5)
	                        ;             ret(6,7,8) count_arg(9,10)
	lda	2,s             ; A = src
	tay                     ; Y = src (MVN destination index)
	tax                     ; X = src (MVN source index)
	lda	9,s             ; A = count
	beq	CLEANUP         ; count == 0 -> nothing to mirror
	sec
	sbc	#1              ; A = count - 1  (MVN copies A+1 bytes)
	mvn	#$01,#$01       ; self-copy in bank $01, shadowed to bank $E1

	; MVN changed the data bank register to $01; restore it below.
CLEANUP:
	plb                     ; restore the data bank register
	pla                     ; drop the stashed src
	ply                     ; restore Y
	rtl

	end
