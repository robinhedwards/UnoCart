;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM
;	Copyright (C) 2008-2016 Avery Lee
;
;	Electrotrains SD Cart ATR PIO module
;	Copyright (C) 2016 Jonathan Halliday
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.



.proc PIOAttemptSIO
	mwa		dbuflo bufrlo
	lda		ddevic
	and 	#$70
	;check for disk IO
	cmp		#$30
	bne		Ignore_Request
	;pass back to OS if not for us
	lda		dunit
	cmp		#1
	bne		Ignore_Request
	;only handling drive 1 at the moment
	lda		dcomnd
	cmp		#'R'
	beq		ReadSector
	cmp		#'W'
	beq		WriteSector
	cmp		#'P'
	beq		WriteSector
	cmp 	#'S'
	beq		GetStatus
;	cmp		#'N'
;	beq		GetPERCOM
	bne		Error
	
Ignore_Request:
	clc
	rts
	
WriteSector:
	bit		dstats	; ensure only bit 7 of dstats is set for write
	bvs		Error
	bpl		Error
	bmi		GetBufferSize
	
ReadSector:
	bit		dstats
	bvc		Error
	bmi		Error
	
GetBufferSize:
	ldx		#0
	
TransferLoop:
	bit		dstats
	bmi		Write

	ldy		#CART_CMD_READ_ATR_SECTOR
	bne		Transfer

GetStatus:
	jsr		GetATRHeader
	bmi		Error
	ldy 	#3
@	
	lda		StatusTable,y
	sta		(bufrlo),y
	dey
	bne		@-
	;get sector size MSB
	lda		#$10
	ldx		CART_ATR_HEADER+5
	seq
	lda		#$30
	sta		(bufrlo),y
	;return Y=1
	iny
	sec
	rts

;GetPERCOM:
;	jsr		GetATRHeader
;	bmi		Error
;	ldy		#11
;@
;	lda		perctbl,y
;	sta		(bufrlo),y
;	dey
;	bpl		@-
;	bmi		ReturnOK
	
	
Error:
	sec
	rts	

Write:
	ldy		#0
	;write 128 bytes
@
	lda		(bufrlo),y
	sta		CART_SECTOR_BUFFER_WRITE,y
	iny
	bpl		@-
	ldy		#CART_CMD_WRITE_ATR_SECTOR

Transfer:
	mva		daux1	CART_DCB_SECTOR_LO
	mva		daux2	CART_DCB_SECTOR_HI
	stx		CART_DCB_PAGE
	
	jsr		IssueCartCommand
	bmi 	Error
	
	bit		dstats	; if this a write operation, we're done
	bmi		CheckNextPage
	;read 128 bytes
	ldy		#0
@
	;watch for speculative read side-effects
	lda		CART_SECTOR_BUFFER_READ,y
	sta		(bufrlo),y
	iny
	bpl		@-
	
CheckNextPage
	cpx 	dbythi
	bcs		ReturnOK
	lda		bufrlo
	clc
	adc		#$80
	sta		bufrlo
	scc
	inc		bufrhi
	inx
	bne		TransferLoop
	
ReturnOK:
	mwa		dbuflo bufrlo
	ldy		#1	; say OK
	sec
	rts



StatusTable
	.byte	$30,$FF,$E0,$00

;PERCTbl
;	.byte 40		; Tracks	
;	.byte 9		 	; Step rate
;	.byte > 720		; Sectors (M) - get partition size from table
;	.byte < 720		; Sectors (L)
;	.byte 0			; Sectors (H)
;	.byte 4			; Type (4=MFM, 6=1.2M, 8=percom+4 is sectors H)
;	.byte > 256		; Bytes / Sec (Hi)
;	.byte < 256		; Bytes / Sec (Lo)
;	.byte $FF		; serial rate control
;	.byte 0,0,0		; misc bytes
.endp

;==============================================================================

.proc PIOWaitReady
	;5 second timeout
	ldy rtclok+2
	dey
@
	lda CART_RESPONSE
	cmp #CART_RESPONSE_READY
	beq Done
	cpy rtclok+2
	bne @-
	;carry set on error
	rts
Done:
	clc
	rts
.endp

;==============================================================================

.proc GetATRHeader
	ldy		#CART_CMD_ATR_HEADER
	;fall into IssueCartCommand
.endp	

;==============================================================================

.proc IssueCartCommand
	;issue command in Y
	sty		CART_CMD
	jsr		PIOWaitReady
	bcs		Timeout
	ldy		CART_STATUS
	bne		BadStatus
	rts
Timeout
	ldy		#$8A
	rts
BadStatus
	;return NAK on bad status for now
	ldy		#$8B
	rts
.endp

;==============================================================================

.proc CheckDOSINI
	lda		DOSINI
	ldx		DOSINI+1
	cmp		#< $0160
	bne 	Chain
	cpx		#> $0160
	beq		OK
Chain
	sta		$0161
	stx		$0162
	mwa		#$0160 DOSINI
OK
	rts
.endp
