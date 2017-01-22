;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Electrotrains SD cart PIO register definitions
;	Copyright (C) 2008-2016 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.


.ifndef f_PIO_INC
.def f_PIO_INC

;The firmware supports two commands (we can elaborate on this later once something is working):

;#define CART_CMD_MOUNT_ATR          0x20
;#define CART_CMD_READ_ATR_SECTOR    0x21

;To initiate a command, write the cmd to $D5DF. e.g. POKE 54751,32 to mount the atr.
 
;Mount ATR takes no parameters, and will return the ATR header of the originally selected ATR file.

;$D500=$11 when the cartridge comes back
;$D501=status (see below)
;$D502 upwards = 16 bytes of ATR header
 
;Read Sector expects:

;$D500 = device (unused for now)
;$D501 = low byte of sector
;$D502 = high byte of sector
;$D503 = 128 byte page of sector (write 0 unless you want the second half of a 256 byte sector, which would be 1)
 
;Read sector will return:

;$D500=$11 when the cartridge comes back
;$D501=status (see below)
;$D502 upwards = 128 bytes of sector data
 
;If the firmware is asked to read off the end of an ATR file, it should just return a blank (all zeros) sector. I did this for truncated ATRs. Not sure if this is right?
 
;$D501 Status on return for both commands can be:

;0 = success
;1 = no media (SD card removed?)
;2 = no file (ATR file is missing)
;3 = bad ATR (ATR file doesn't have correct header)

CART_CMD_MOUNT_ATR			equ $20
CART_CMD_READ_ATR_SECTOR	equ $21
CART_CMD_WRITE_ATR_SECTOR	equ $22
CART_CMD_ATR_HEADER			equ $23

CART_CMD		equ $D5DF

CART_RESPONSE		equ $D500
CART_STATUS			equ $D501
CART_ATR_HEADER		equ $D502

CART_DCB_DEVICE		equ $D500
CART_DCB_SECTOR_LO	equ $D501
CART_DCB_SECTOR_HI	equ $D502
CART_DCB_PAGE		equ $D503

CART_SECTOR_BUFFER_READ	equ $D502
CART_SECTOR_BUFFER_WRITE equ $D504

CART_RESPONSE_READY	equ $11

CART_STATUS_OK			equ 0
CART_STATUS_NO_MEDIA	equ 1
CART_STATUS_NO_FILE		equ 2
CART_STATUS_BAD_ATR		equ 3

.endif