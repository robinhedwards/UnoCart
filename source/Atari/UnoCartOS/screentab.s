;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Screen Handler (data - separated from screen module by JH)
;	Copyright (C) 2008-2016 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.


;Display list:
;	24 blank lines (3 bytes)
;	initial mode line with LMS (3 bytes)
;	mode lines
;	LMS for modes >4 pages
;	wait VBL (3 bytes)
;
;	total is 8-10 bytes + mode lines

; These are the addresses produced by the normal XL/XE OS:
;
;               Normal       Split, coarse    Split, fine
; Mode       DL   PF   TX     DL   PF   TX    DL   PF   TX
;  0        9C20 9C40 9F60   9C20 9C40 9F60  9C1F 9C40 9F60
;  1        9D60 9D80 9F60   9D5E 9D80 9F60  9D5D 9D80 9F60
;  2        9E5C 9E70 9F60   9E58 9E70 9F60  9E57 9E70 9F60
;  3        9E50 9E70 9F60   9E4E 9E70 9F60  9E4D 9E70 9F60
;  4        9D48 9D80 9F60   9D4A 9D80 9F60  9D49 9D80 9F60
;  5        9B68 9BA0 9F60   9B6A 9BA0 9F60  9B69 9BA0 9F60
;  6        9778 97E0 9F60   9782 97E0 9F60  9781 97E0 9F60
;  7        8F98 9060 9F60   8FA2 9060 9F60  8FA1 9060 9F60
;  8        8036 8150 9F60   8050 8150 9F60  804F 8150 9F60
;  9        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 10        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 11        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 12        9B80 9BA0 9F60   9B7E 9BA0 9F60  9B7D 9BA0 9F60
; 13        9D6C 9D80 9F60   9D68 9D80 9F60  9D67 9D80 9F60
; 14        8F38 9060 9F60   8F52 9060 9F60  8F51 9060 9F60
; 15        8036 8150 9F60   8050 8150 9F60  804F 8150 9F60
;
; *DL = display list (SDLSTL/SDLSTH)
; *PF = playfield (SAVMSC)
; *TX = text window (TXTMSC)
;
; From this, we can derive a few things:
;	- The text window is always 160 ($A0) bytes below the ceiling.
;	- The playfield is always positioned to have enough room for
;	  the text window, even though this wastes a little bit of
;	  memory for modes 1, 2, 3, 4, and 13. This means that the
;	  PF address does not have to be adjusted for split mode.
;	- The display list and playfield addresses are sometimes
;	  adjusted in order to avoid crossing 1K boundaries for the
;	  display list (gr.7) and 4K boundaries for the playfield (gr.8).
;	  However, these are fixed offsets -- adjusting RAMTOP to $9F
;	  does not remove the DL padding in GR.7 and breaks GR.7/8.
;	- Fine-scrolled modes take one additional byte for the extra
;	  mode 2 line. In fact, it displays garbage that is masked by
;	  a DLI that sets COLPF1 equal to COLPF2. (!)
;
; You might ask, why bother replicating these? Well, there are a
; number of programs that rely on the layout of the default screen
; and break if the memory addressing is different, such as ForemXEP.

.macro _SCREEN_TABLES_2

;Mode	Type	Res		Colors	ANTIC	Mem(unsplit)	Mem(split)
; 0		Text	40x24	1.5		2		960+32 (4)		960+32 (4)
; 1		Text	20x24	5		6		480+32 (2)		560+32 (3)
; 2		Text	20x12	5		7		240+20 (2)		360+22 (2)
; 3		Bitmap	40x24	4		8		240+32 (2)		360+32 (2)
; 4		Bitmap	80x48	2		9		480+56 (3)		560+52 (3)
; 5		Bitmap	80x48	4		A		960+56 (4)		960+52 (4)
; 6		Bitmap	160x96	2		B		1920+104 (8)	1760+92 (8)
; 7		Bitmap	160x96	4		D		3840+104 (16)	3360+92 (14)
; 8		Bitmap	320x192	1.5		F		7680+202 (32)	6560+174 (27)
; 9		Bitmap	80x192	16		F		7680+202 (32)	6560+174 (27)
; 10	Bitmap	80x192	9		F		7680+202 (32)	6560+174 (27)
; 11	Bitmap	80x192	16		F		7680+202 (32)	6560+174 (27)
; 12	Text	40x24	5		4		960+32 (4)		960+32 (4)
; 13	Text	40x12	5		5		480+20 (2)		560+24 (3)
; 14	Bitmap	160x192	2		C		3840+200 (16)	3360+172 (14)
; 15	Bitmap	160x192	4		E		7680+202 (32)	6560+172 (27)

;==========================================================================
;
.proc ScreenPlayfieldSizesLo
	dta	<($10000-$03C0)			;gr.0	 960 bytes = 40*24              = 40*24
	dta	<($10000-$0280)			;gr.1	 640 bytes = 20*24  + 40*4      = 40*12  + 40*4
	dta	<($10000-$0190)			;gr.2	 400 bytes = 10*24  + 40*4      = 40*6   + 40*4
	dta	<($10000-$0190)			;gr.3	 400 bytes = 10*24  + 40*4      = 40*6   + 40*4
	dta	<($10000-$0280)			;gr.4	 640 bytes = 10*48  + 40*4      = 40*12  + 40*4
	dta	<($10000-$0460)			;gr.5	1120 bytes = 20*48  + 40*4      = 40*24  + 40*4
	dta	<($10000-$0820)			;gr.6	2080 bytes = 20*96  + 40*4      = 40*48  + 40*4
	dta	<($10000-$0FA0)			;gr.7	4000 bytes = 40*96  + 40*4      = 40*96  + 40*4
	dta	<($10000-$1EB0)			;gr.8	7856 bytes = 40*192 + 40*4 + 16 = 40*192 + 40*4 + 16
	dta	<($10000-$1EB0)			;gr.9	7856 bytes = 40*192 + 40*4 + 16 = 40*192 + 40*4 + 16
	dta	<($10000-$1EB0)			;gr.10	7856 bytes = 40*192 + 40*4 + 16 = 40*192 + 40*4 + 16
	dta	<($10000-$1EB0)			;gr.11	7856 bytes = 40*192 + 40*4 + 16 = 40*192 + 40*4 + 16
	dta	<($10000-$0460)			;gr.12	1120 bytes = 40*24  + 40*4      = 40*24  + 40*4
	dta	<($10000-$0280)			;gr.13	 640 bytes = 40*12  + 40*4      = 40*12  + 40*4
	dta	<($10000-$0FA0)			;gr.14	4000 bytes = 20*192 + 40*4      = 40*96  + 40*4
	dta	<($10000-$1EB0)			;gr.15	7856 bytes = 40*192 + 40*4 + 16 = 40*192 + 40*4 + 16
.endp

.proc ScreenPlayfieldSizesHi
	dta	>($10000-$03C0)			;gr.0
	dta	>($10000-$0280)			;gr.1
	dta	>($10000-$0190)			;gr.2
	dta	>($10000-$0190)			;gr.3
	dta	>($10000-$0280)			;gr.4
	dta	>($10000-$0460)			;gr.5
	dta	>($10000-$0820)			;gr.6
	dta	>($10000-$0FA0)			;gr.7
	dta	>($10000-$1EB0)			;gr.8
	dta	>($10000-$1EB0)			;gr.9
	dta	>($10000-$1EB0)			;gr.10
	dta	>($10000-$1EB0)			;gr.11
	dta	>($10000-$0460)			;gr.12
	dta	>($10000-$0280)			;gr.13
	dta	>($10000-$0FA0)			;gr.14
	dta	>($10000-$1EB0)			;gr.15
.endp

;==========================================================================
; ANTIC mode is in bits 0-3, PRIOR bits in 6-7.
; DL 1K hop: bit 4
; playfield 4K hop: bit 5
;
.proc ScreenModeTable
	dta		$02,$06,$07,$08,$09,$0A,$0B,$1D,$3F,$7F,$BF,$FF,$04,$05,$1C,$3E
.endp

;==========================================================================
;
.proc ScreenHeightShifts
	dta		1
	dta		1
	dta		0
	dta		1
	dta		2
	dta		2
	dta		3
	dta		3
	dta		4
	dta		4
	dta		4
	dta		4
	dta		1
	dta		0
	dta		4
	dta		4
.endp

.proc ScreenHeights
	dta		12, 24, 48, 96, 192
.endp

.proc ScreenPixelWidthIds
	dta		1		;gr.0	40 pixels
	dta		0		;gr.1	20 pixels
	dta		0		;gr.2	20 pixels
	dta		1		;gr.3	40 pixels
	dta		2		;gr.4	80 pixels
	dta		2		;gr.5	80 pixels
	dta		3		;gr.6	160 pixels
	dta		3		;gr.7	160 pixels
	dta		4		;gr.8	320 pixels
	dta		2		;gr.9	80 pixels
	dta		2		;gr.10	80 pixels
	dta		2		;gr.11	80 pixels
	dta		1		;gr.12	40 pixels
	dta		1		;gr.13	40 pixels
	dta		3		;gr.14	160 pixels
	dta		3		;gr.15	160 pixels
.endp
.endm

ScreenHeightsSplit = ScreenWidths
;	dta		10, 20, 40, 80, 160

ScreenPixelWidthsLo = ScreenWidths + 1

.macro _SCREEN_TABLES_1

.proc ScreenWidths
	dta		<10
	dta		<20
	dta		<40
	dta		<80
	dta		<160
	dta		<320
.endp

.proc ScreenPixelWidthsHi
	dta		>20
	dta		>40
	dta		>80
	dta		>160
	dta		>320
.endp

.proc ScreenEncodingTab
	dta		0		;gr.0	direct bytes
	dta		0		;gr.1	direct bytes
	dta		0		;gr.2	direct bytes
	dta		2		;gr.3	two bits per pixel
	dta		3		;gr.4	one bit per pixel
	dta		2		;gr.5	two bits per pixel
	dta		3		;gr.6	one bit per pixel
	dta		2		;gr.7	two bits per pixel
	dta		3		;gr.8	one bit per pixel
	dta		1		;gr.9	four bits per pixel
	dta		1		;gr.10	four bits per pixel
	dta		1		;gr.11	four bits per pixel
	dta		0		;gr.12	direct bytes
	dta		0		;gr.13	direct bytes
	dta		3		;gr.14	one bit per pixel
	dta		2		;gr.15	two bits per pixel
.endp

.proc ScreenPixelMasks
	dta		$ff, $0f, $03, $01, $ff, $f0, $c0, $80
.endp
.endm

.macro _SCREEN_TABLES_3
.proc ScreenEncodingTable
	dta		$00,$11,$22,$33,$44,$55,$66,$77,$88,$99,$aa,$bb,$cc,$dd,$ee,$ff
	dta		$00,$55,$aa,$ff
	dta		$00,$ff
.endp
.endm

.if _KERNEL_XLXE
	_SCREEN_TABLES_3
	_SCREEN_TABLES_2
	_SCREEN_TABLES_1
.endif
