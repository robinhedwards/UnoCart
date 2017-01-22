;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement - Blackboard
;	Copyright (C) 2008-2016 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

.proc Blackboard
	;print banner
	mva		#<banner icbal
	mva		#>banner icbah
	sta		icbll
	ldx		#0
	stx		icblh
	lda		#CIOCmdPutRecord
echoloop:
	sta		iccmd
	jsr		ciov

	stx		icbll
	lda		#CIOCmdGetChars
	bne		echoloop

banner:
	dta		'Altirra BIOS - memo pad',$9B

.endp
