/*
 * --------------------------------------
 * UNOCart Firmware (c)2016 Robin Edwards
 * --------------------------------------
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include "defines.h"
#include "stm32f4xx.h"
#include "tm_stm32f4_fatfs.h"
#include "tm_stm32f4_delay.h"
#include <stdio.h>
#include <string.h>

#include "rom.h" /* unsigned char cart_rom[64*1024] */
#include "osrom.h"

unsigned char cart_ram1[64*1024];
unsigned char cart_ram2[64*1024] __attribute__((section(".ccmram")));
unsigned char cart_d5xx[256] = {0};
char errorBuf[40];

#define CART_CMD_OPEN_ITEM			0x00
#define CART_CMD_READ_CUR_DIR		0x01
#define CART_CMD_GET_DIR_ENTRY		0x02
#define CART_CMD_UP_DIR				0x03
#define CART_CMD_ROOT_DIR			0x04
#define CART_CMD_SEARCH				0x05
#define CART_CMD_LOAD_SOFT_OS		0x10
#define CART_CMD_SOFT_OS_CHUNK		0x11
#define CART_CMD_MOUNT_ATR			0x20	// unused, done automatically by firmware
#define CART_CMD_READ_ATR_SECTOR	0x21
#define CART_CMD_WRITE_ATR_SECTOR	0x22
#define CART_CMD_ATR_HEADER			0x23
#define CART_CMD_NO_CART			0xFE
#define CART_CMD_ACTIVATE_CART  	0xFF

#define CART_TYPE_NONE				0
#define CART_TYPE_8K				1	// 8k
#define CART_TYPE_16K				2	// 16k
#define CART_TYPE_XEGS_32K			3	// 32k
#define CART_TYPE_XEGS_64K			4	// 64k
#define CART_TYPE_XEGS_128K			5	// 128k
#define CART_TYPE_SW_XEGS_32K		6	// 32k
#define CART_TYPE_SW_XEGS_64K		7	// 64k
#define CART_TYPE_SW_XEGS_128K		8	// 128k
#define CART_TYPE_MEGACART_16K		9	// 16k
#define CART_TYPE_MEGACART_32K		10	// 32k
#define CART_TYPE_MEGACART_64K		11	// 64k
#define CART_TYPE_MEGACART_128K		12	// 128k
#define CART_TYPE_BOUNTY_BOB		13	// 40k
#define CART_TYPE_ATARIMAX_1MBIT	14	// 128k
#define CART_TYPE_WILLIAMS_64K		15	// 32k/64k
#define CART_TYPE_OSS_16K_TYPE_B	16	// 16k
#define CART_TYPE_OSS_8K			17	// 8k
#define CART_TYPE_OSS_16K_034M		18	// 16k
#define CART_TYPE_OSS_16K_043M		19	// 16k
#define CART_TYPE_SIC_128K			20	// 128k
#define CART_TYPE_SDX_64K			21	// 64k
#define CART_TYPE_SDX_128K			22	// 128k
#define CART_TYPE_DIAMOND_64K		23	// 64k
#define CART_TYPE_EXPRESS_64K		24	// 64k
#define CART_TYPE_BLIZZARD_16K		25	// 16k
#define CART_TYPE_ATR				254
#define CART_TYPE_XEX				255

typedef struct {
	char isDir;
	char filename[13];
	char long_filename[32];
	char full_path[210];
} DIR_ENTRY;	// 256 bytes = 256 entries in 64k

int num_dir_entries = 0; // how many entries in the current directory

int entry_compare(const void* p1, const void* p2)
{
	DIR_ENTRY* e1 = (DIR_ENTRY*)p1;
	DIR_ENTRY* e2 = (DIR_ENTRY*)p2;
	if (e1->isDir && !e2->isDir) return -1;
	else if (!e1->isDir && e2->isDir) return 1;
	else return stricmp(e1->long_filename, e2->long_filename);
}

char *get_filename_ext(char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

int is_valid_file(char *filename) {
	char *ext = get_filename_ext(filename);
	if (stricmp(ext, "CAR") == 0 || stricmp(ext, "ROM") == 0
			|| stricmp(ext, "XEX") == 0 || stricmp(ext, "ATR") == 0)
		return 1;
	return 0;
}

// single FILINFO structure
FILINFO fno;
char lfn[_MAX_LFN + 1];   /* Buffer to store the LFN */

void init() {
	// this seems to be required for this version of FAT FS
	fno.lfname = lfn;
	fno.lfsize = sizeof lfn;
}

int scan_files(char *path, char *search)
{
    FRESULT res;
    DIR dir;
    UINT i;

	res = f_opendir(&dir, path);
	if (res == FR_OK) {
		for (;;) {
			if (num_dir_entries == 255) break;
			res = f_readdir(&dir, &fno);
			if (res != FR_OK || fno.fname[0] == 0) break;
			if (fno.fattrib & (AM_HID | AM_SYS)) continue;
			if (fno.fattrib & AM_DIR) {
				i = strlen(path);
				strcat(path, "/");
				strcat(path, fno.fname);
				if (strlen(path) >= 210) continue;	// no more room for path in DIR_ENTRY
				res = scan_files(path, search);
				if (res != FR_OK) break;
				path[i] = 0;
			}
			else if (is_valid_file(fno.fname))
			{
				// copy the 8.3 filename to the LFN if blank
				if (!fno.lfname[0]) strcpy(fno.lfname, fno.fname);
				char *match = strcasestr(fno.lfname, search);
				if (match) {
					DIR_ENTRY *dst = (DIR_ENTRY *)&cart_ram1[0];
					dst += num_dir_entries;
					// fill out a record
					dst->isDir = (match == fno.lfname) ? 1 : 0;	// use this for a "score"
					strcpy(dst->filename, fno.fname);
					strncpy(dst->long_filename, fno.lfname, 31);
					dst->long_filename[31] = 0;
					// full path for search results
					strcpy(dst->full_path, path);

					num_dir_entries++;
				}
			}
		}
		f_closedir(&dir);
	}
	return res;
}

int search_directory(char *path, char *search) {
	char pathBuf[256];
	strcpy(pathBuf, path);
	num_dir_entries = 0;
	int i;
	FATFS FatFs;
	TM_DELAY_Init();
	if (f_mount(&FatFs, "", 1) == FR_OK) {
		if (scan_files(pathBuf, search) == FR_OK) {
			// sort by score, name
			qsort((DIR_ENTRY *)&cart_ram1[0], num_dir_entries, sizeof(DIR_ENTRY), entry_compare);
			DIR_ENTRY *dst = (DIR_ENTRY *)&cart_ram1[0];
			// reset the "scores" back to 0
			for (i=0; i<num_dir_entries; i++)
				dst[i].isDir = 0;
			return 1;

		}
	}
	strcpy(errorBuf, "Problem searching SD card");
	return 0;
}

int read_directory(char *path) {
	int ret = 0;
	num_dir_entries = 0;
	DIR_ENTRY *dst = (DIR_ENTRY *)&cart_ram1[0];

	FATFS FatFs;
	TM_DELAY_Init();
	if (f_mount(&FatFs, "", 1) == FR_OK) {
		DIR dir;
		if (f_opendir(&dir, path) == FR_OK) {
			while (num_dir_entries < 255) {
				if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
					break;
				if (fno.fattrib & (AM_HID | AM_SYS))
					continue;
				dst->isDir = fno.fattrib & AM_DIR ? 1 : 0;
				if (!dst->isDir)
					if (!is_valid_file(fno.fname)) continue;
				// copy file record to first ram block
	            strcpy(dst->filename, fno.fname);
				if (fno.lfname[0]) {
					strncpy(dst->long_filename, fno.lfname, 31);
					dst->long_filename[31] = 0;
				}
				else strcpy(dst->long_filename, fno.fname);
				dst->full_path[0] = 0; // path only for search results
	            dst++;
				num_dir_entries++;
			}
			f_closedir(&dir);
		}
		else
			strcpy(errorBuf, "Can't read directory");
		f_mount(0, "", 1);
		qsort((DIR_ENTRY *)&cart_ram1[0], num_dir_entries, sizeof(DIR_ENTRY), entry_compare);
		ret = 1;
	}
	else
		strcpy(errorBuf, "Can't read SD card");
	return ret;
}

/* ATR Handling */

// ATR format
#define ATR_HEADER_SIZE 16
#define ATR_SIGNATURE 0x0296
typedef struct {
  uint16_t signature;
  uint16_t pars;
  uint16_t secSize;
  uint16_t parsHigh;
  uint8_t flags;
  uint16_t protInfo;
  uint8_t unused[5];
} ATRHeader;

typedef struct {
	char path[256];
	ATRHeader atrHeader;
	int	filesize;
	FIL fil;
} MountedATR;

MountedATR mountedATRs[1] = {0};

FATFS FatFs;
int doneFatFsInit = 0;

int mount_atr(char *filename) {
	// returns 0 for success or error code
	// 1 = no media, 2 = no file, 3 = bad atr
	if (!doneFatFsInit) {
		if (f_mount(&FatFs, "", 1) != FR_OK)
			return 1;
		doneFatFsInit = 1;
	}
	MountedATR *mountedATR = &mountedATRs[0];
	if (f_open(&mountedATR->fil, filename, FA_READ|FA_WRITE) != FR_OK)
		return 2;
	UINT br;
	if (f_read(&mountedATR->fil, &mountedATR->atrHeader, ATR_HEADER_SIZE, &br) != FR_OK || br != ATR_HEADER_SIZE ||
			mountedATR->atrHeader.signature != ATR_SIGNATURE) {
		f_close(&mountedATR->fil);
		return 3;
	}
	// success
	strcpy(mountedATR->path, filename);
	mountedATR->filesize = f_size(&mountedATR->fil);
	return 0;
}

int read_atr_sector(uint16_t sector, uint8_t page, uint8_t *buf) {
	// returns 0 for success or error code
	// 1 = no ATR mounted, 2 = invalid sector
	MountedATR *mountedATR = &mountedATRs[0];
	if (!mountedATR->path[0]) return 1;
	if (sector == 0) return 2;

	int offset = ATR_HEADER_SIZE;
	// first 3 sectors are always 128 bytes
	if (sector <=3)
		offset += (sector - 1) * 128;
	else
		offset += (3 * 128) + ((sector - 4) * mountedATR->atrHeader.secSize) + (page * 128);
	// check we're not reading beyond the end of the file..
	if (offset > (mountedATR->filesize - 128)) {
		memset(buf, 0 , 128);	// return blank sector?
		return 0;
	}
	UINT br;
	if (f_lseek(&mountedATR->fil, offset) != FR_OK || f_read(&mountedATR->fil, buf, 128, &br) != FR_OK || br != 128)
		return 2;
	return 0;
}

int write_atr_sector(uint16_t sector, uint8_t page, uint8_t *buf) {
	// returns 0 for success or error code
	// 1 = no ATR mounted, 2 = write error
	MountedATR *mountedATR = &mountedATRs[0];
	if (!mountedATR->path[0]) return 1;
	if (sector == 0) return 2;

	int offset = ATR_HEADER_SIZE;
	// first 3 sectors are always 128 bytes
	if (sector <=3)
		offset += (sector - 1) * 128;
	else
		offset += (3 * 128) + ((sector - 4) * mountedATR->atrHeader.secSize) + (page * 128);
	// check we're not writing beyond the end of the file..
	if (offset > (mountedATR->filesize - 128))
		return 2;
	UINT bw;
	if (f_lseek(&mountedATR->fil, offset) != FR_OK || f_write(&mountedATR->fil, buf, 128, &bw) != FR_OK || f_sync(&mountedATR->fil) != FR_OK  || bw != 128)
		return 2;
	return 0;
}

/* CARTRIDGE/XEX HANDLING */

int load_file(char *filename) {
	TM_DELAY_Init();
	FATFS FatFs;
	int cart_type = CART_TYPE_NONE;
	int car_file = 0, xex_file = 0, expectedSize = 0;
	unsigned char carFileHeader[16];
	UINT br, size = 0;

	if (strncasecmp(filename+strlen(filename)-4, ".CAR", 4) == 0)
		car_file = 1;
	if (strncasecmp(filename+strlen(filename)-4, ".XEX", 4) == 0)
		xex_file = 1;

	if (f_mount(&FatFs, "", 1) != FR_OK) {
		strcpy(errorBuf, "Can't read SD card");
		return 0;
	}
	FIL fil;
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		strcpy(errorBuf, "Can't open file");
		goto cleanup;
	}

	// read the .CAR file header?
	if (car_file) {
		if (f_read(&fil, carFileHeader, 16, &br) != FR_OK || br != 16) {
			strcpy(errorBuf, "Bad CAR file");
			goto closefile;
		}
		int car_type = carFileHeader[7];
		if (car_type == 1)			{ cart_type = CART_TYPE_8K; expectedSize = 8192; }
		else if (car_type == 2)		{ cart_type = CART_TYPE_16K; expectedSize = 16384; }
		else if (car_type == 3) 	{ cart_type = CART_TYPE_OSS_16K_034M; expectedSize = 16384; }
		else if (car_type == 8)		{ cart_type = CART_TYPE_WILLIAMS_64K; expectedSize = 65536; }
		else if (car_type == 9)		{ cart_type = CART_TYPE_EXPRESS_64K; expectedSize = 65536; }
		else if (car_type == 10)	{ cart_type = CART_TYPE_DIAMOND_64K; expectedSize = 65536; }
		else if (car_type == 11)	{ cart_type = CART_TYPE_SDX_64K; expectedSize = 65536; }
		else if (car_type == 12) 	{ cart_type = CART_TYPE_XEGS_32K; expectedSize = 32768; }
		else if (car_type == 13) 	{ cart_type = CART_TYPE_XEGS_64K; expectedSize = 65536; }
		else if (car_type == 14) 	{ cart_type = CART_TYPE_XEGS_128K; expectedSize = 131072; }
		else if (car_type == 15) 	{ cart_type = CART_TYPE_OSS_16K_TYPE_B; expectedSize = 16384; }
		else if (car_type == 18) 	{ cart_type = CART_TYPE_BOUNTY_BOB; expectedSize = 40960; }
		else if (car_type == 22)	{ cart_type = CART_TYPE_WILLIAMS_64K; expectedSize = 32768; }
		else if (car_type == 26)	{ cart_type = CART_TYPE_MEGACART_16K; expectedSize = 16384; }
		else if (car_type == 27)	{ cart_type = CART_TYPE_MEGACART_32K; expectedSize = 32768; }
		else if (car_type == 28)	{ cart_type = CART_TYPE_MEGACART_64K; expectedSize = 65536; }
		else if (car_type == 29)	{ cart_type = CART_TYPE_MEGACART_128K; expectedSize = 131072; }
		else if (car_type == 33)	{ cart_type = CART_TYPE_SW_XEGS_32K; expectedSize = 32768; }
		else if (car_type == 34)	{ cart_type = CART_TYPE_SW_XEGS_64K; expectedSize = 65536; }
		else if (car_type == 35)	{ cart_type = CART_TYPE_SW_XEGS_128K; expectedSize = 131072; }
		else if (car_type == 40)	{ cart_type = CART_TYPE_BLIZZARD_16K; expectedSize = 16384; }
		else if (car_type == 41)	{ cart_type = CART_TYPE_ATARIMAX_1MBIT; expectedSize = 131072; }
		else if (car_type == 43)	{ cart_type = CART_TYPE_SDX_128K; expectedSize = 131072; }
		else if (car_type == 44)	{ cart_type = CART_TYPE_OSS_8K; expectedSize = 8192; }
		else if (car_type == 45) 	{ cart_type = CART_TYPE_OSS_16K_043M; expectedSize = 16384; }
		else if (car_type == 54)	{ cart_type = CART_TYPE_SIC_128K; expectedSize = 131072; }
		else {
			strcpy(errorBuf, "Unsupported CAR type");
			goto closefile;
		}
	}

	// set a default error
	strcpy(errorBuf, "Can't read file");

	unsigned char *dst = &cart_ram1[0];
	int bytes_to_read = 64 * 1024;
	if (xex_file) {
		dst += 4;	// leave room for the file length at the start of sram
		bytes_to_read -= 4;
	}
	// read the file in two 64k chunks to each area of SRAM
	if (f_read(&fil, dst, bytes_to_read, &br) != FR_OK) {
		cart_type = CART_TYPE_NONE;
		goto closefile;
	}
	size += br;
	if (br == bytes_to_read) {
		// first 64k was complete, so try to load 64k more
		if (f_read(&fil, &cart_ram2[0], 64*1024, &br) != FR_OK) {
			cart_type = CART_TYPE_NONE;
			goto closefile;
		}
		size += br;
		if (br == 64*1024) {
			// that's 128k read, is there any more?
			if (f_read(&fil, carFileHeader, 1, &br) != FR_OK) {
				cart_type = CART_TYPE_NONE;
				goto closefile;
			}
			if	(br == 1) {
				strcpy(errorBuf, "Cart file/XEX too big (>128k)");
				cart_type = CART_TYPE_NONE;
				goto closefile;
			}
		}
	}

	// set the correct cart type based on the size
	if (car_file) {
		if (size != expectedSize) {
			strcpy(errorBuf, "CAR file is wrong size");
			cart_type = CART_TYPE_NONE;
			goto closefile;
		}
	}
	else if (xex_file) {
		cart_type = CART_TYPE_XEX;
		// stick the size of the file as the first 4 bytes (little endian)
		cart_ram1[0] = size & 0xFF;
		cart_ram1[1] = (size >> 8) & 0xFF;
		cart_ram1[2] = (size >> 16) & 0xFF;
		cart_ram1[3] = 0;	// has to be zero!
	}
	else {	// not a car/xex file - guess the type based on size
		if (size == 8*1024) cart_type = CART_TYPE_8K;
		else if (size == 16*1024) cart_type = CART_TYPE_16K;
		else if (size == 32*1024) cart_type = CART_TYPE_XEGS_32K;
		else if (size == 64*1024) cart_type = CART_TYPE_XEGS_64K;
		else if (size == 128*1024) cart_type = CART_TYPE_XEGS_128K;
		else {
			strcpy(errorBuf, "Unsupported ROM size ");
			cart_type = CART_TYPE_NONE;
			goto closefile;
		}
	}

closefile:
	f_close(&fil);
cleanup:
	f_mount(0, "", 1);
	return cart_type;
}

#define RD5_LOW GPIOB->BSRRH = GPIO_Pin_2;
#define RD4_LOW GPIOB->BSRRH = GPIO_Pin_4;
#define RD5_HIGH GPIOB->BSRRL = GPIO_Pin_2;
#define RD4_HIGH GPIOB->BSRRL = GPIO_Pin_4;

#define CONTROL_IN GPIOC->IDR
#define ADDR_IN GPIOD->IDR
#define DATA_IN GPIOE->IDR
#define DATA_OUT GPIOE->ODR

#define PHI2_RD (GPIOC->IDR & 0x0001)
#define S5_RD (GPIOC->IDR & 0x0002)
#define S4_RD (GPIOC->IDR & 0x0004)
#define S4_AND_S5_HIGH (GPIOC->IDR & 0x0006) == 0x6

#define PHI2	0x0001
#define S5		0x0002
#define S4		0x0004
#define CCTL	0x0010
#define RW		0x0020

#define SET_DATA_MODE_IN GPIOE->MODER = 0x00000000;
#define SET_DATA_MODE_OUT GPIOE->MODER = 0x55550000;

#define GREEN_LED_OFF GPIOB->BSRRH = GPIO_Pin_0;
#define RED_LED_OFF GPIOB->BSRRH = GPIO_Pin_1;
#define GREEN_LED_ON GPIOB->BSRRL = GPIO_Pin_0;
#define RED_LED_ON GPIOB->BSRRL = GPIO_Pin_1;

GPIO_InitTypeDef  GPIO_InitStructure;

/* Green LED -> PB0, Red LED -> PB1, RD5 -> PB2, RD4 -> PB4 */
void config_gpio_leds_RD45()
{
	/* GPIOB Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	/* Configure PB0, PB1in output pushpull mode */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/* Input Signals GPIO pins on CLK -> PC0, /S5 -> PC1, /S4 ->PC2, CCTL -> PC4, R/W -> PC5 */
void config_gpio_sig(void) {
	/* GPIOC Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/* Input/Output data GPIO pins on PE{8..15} */
void config_gpio_data(void) {
	/* GPIOE Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin =
		GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;	// avoid sharp edges
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
}

/* Input Address GPIO pins on PD{0..15} */
void config_gpio_addr(void) {
	/* GPIOD Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin =
		GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
		GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 |
		GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOD, &GPIO_InitStructure);
}

/*
 Theory of Operation
 -------------------
 Atari sends command to mcu on cart by writing to $D5DF ($D5E0-$D5FF = SDX)
 (extra paramters for the command in $D500-$D5DE)
 Atari must be running from RAM when it sends a command, since the mcu on the cart will
 go away at that point.
 Atari polls $D500 until it reads $11. At this point it knows the mcu is back
 and it is safe to rts back to code in cartridge ROM again.
 Results of the command are in $D501-$D5DF
*/

int emulate_boot_rom(int atrMode) {
	__disable_irq();	// Disable interrupts
	if (atrMode) RD5_LOW else RD5_HIGH
	RD4_LOW
	cart_d5xx[0x00] = 0x11;	// signal that we are here
	uint16_t addr, data, c;
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & CCTL)) {
			// CCTL low
			if (c & RW) {
				// read
				SET_DATA_MODE_OUT
				addr = ADDR_IN;
				DATA_OUT = ((uint16_t)cart_d5xx[addr&0xFF])<<8;
				// wait for phi2 low
				while (CONTROL_IN & PHI2) ;
				SET_DATA_MODE_IN
			}
			else {
				// write
				addr = ADDR_IN;
				data = DATA_IN;
				// read data bus on falling edge of phi2
				while (CONTROL_IN & PHI2)
					data = DATA_IN;
				cart_d5xx[addr&0xFF] = data>>8;
				if ((addr&0xFF) == 0xDF)	// write to $D5DF
					break;
			}
		}
		if (!(c & S5)) {
			// normal cartridge read
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(UnoCart_rom[addr]))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
	}
	__enable_irq();
	return data>>8;
}

void emulate_standard_8k() {
	// 8k
	RD5_HIGH
	uint16_t addr;
	while (1)
	{
		// wait for s5 low
		while (S5_RD) ;
		SET_DATA_MODE_OUT
		// while s5 low
		while (!S5_RD) {
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[addr])<<8;
		}
		SET_DATA_MODE_IN
	}
}

void emulate_standard_16k() {
	// 16k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr;
	while (1)
	{
		// wait for either s4 or s5 low
		while (S4_AND_S5_HIGH) ;
		SET_DATA_MODE_OUT
		if (!S4_RD) {
			// while s4 low
			while (!S4_RD) {
				addr = ADDR_IN;
				DATA_OUT = ((uint16_t)cart_ram1[addr])<<8;
			}
		}
		else {
			// while s5 low
			while (!S5_RD) {
				addr = ADDR_IN;
				DATA_OUT = ((uint16_t)cart_ram1[0x2000|addr])<<8;
			}
		}
		SET_DATA_MODE_IN
	}
}

void emulate_XEGS_32k(char switchable) {
	// 32k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, data, c;
	unsigned char *bankPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(bankPtr+addr)))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[0x6000|addr])<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & CCTL) && !(c & RW)) {
			// CCTL low + write
			data = DATA_IN;
			// read the data bus on falling edge of phi2
			while (CONTROL_IN & PHI2)
				data = DATA_IN;
			// new bank is the low 2 bits written to $D5xx
			bankPtr = &cart_ram1[0] + (8192*((data>>8) & 3));
			if (switchable) {
				if (data & 0x8000) {
					RD4_LOW
					RD5_LOW
					GREEN_LED_OFF
				} else {
					RD4_HIGH
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
	}
}

void emulate_XEGS_64k(char switchable) {
	// 64k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, data, c;
	unsigned char *bankPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(bankPtr+addr)))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[0xE000|addr])<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & CCTL) && !(c & RW)) {
			// CCTL low + write
			data = DATA_IN;
			// read the data bus on falling edge of phi2
			while (CONTROL_IN & PHI2)
				data = DATA_IN;
			// new bank is the low 3 bits written to $D5xx
			bankPtr = &cart_ram1[0] + (8192*((data>>8) & 7));
			if (switchable) {
				if (data & 0x8000) {
					RD4_LOW
					RD5_LOW
					GREEN_LED_OFF
				} else {
					RD4_HIGH
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
	}
}

void emulate_XEGS_128k(char switchable) {
	// 128k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, data, c;
	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram2[0xE000|addr])<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & CCTL) && !(c & RW)) {
			// CCTL low + write
			data = DATA_IN;
			// read the data bus on falling edge of phi2
			while (CONTROL_IN & PHI2)
				data = DATA_IN;
			// new bank is the low 4 bits written to $D5xx
			int bank = (data>>8) & 0xF;
			if (bank & 0x8) ramPtr = &cart_ram2[0]; else ramPtr = &cart_ram1[0];
			ramPtr += 8192 * (bank & 0x7);
			if (switchable) {
				if (data & 0x8000) {
					RD4_LOW
					RD5_LOW
					GREEN_LED_OFF
				} else {
					RD4_HIGH
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
	}
}

void emulate_bounty_bob() {
	// 40k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, c;
	unsigned char *bankPtr1 = &cart_ram1[0], *bankPtr2 =  &cart_ram1[0x4000];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			if (addr & 0x1000) {
				DATA_OUT = ((uint16_t)(*(bankPtr2+(addr&0xFFF))))<<8;
				if (addr == 0x1FF6) bankPtr2 = &cart_ram1[0x4000];
				else if (addr == 0x1FF7) bankPtr2 = &cart_ram1[0x5000];
				else if (addr == 0x1FF8) bankPtr2 = &cart_ram1[0x6000];
				else if (addr == 0x1FF9) bankPtr2 = &cart_ram1[0x7000];
			}
			else {
				DATA_OUT = ((uint16_t)(*(bankPtr1+(addr&0xFFF))))<<8;
				if (addr == 0x0FF6) bankPtr1 = &cart_ram1[0];
				else if (addr == 0x0FF7) bankPtr1 = &cart_ram1[0x1000];
				else if (addr == 0x0FF8) bankPtr1 = &cart_ram1[0x2000];
				else if (addr == 0x0FF9) bankPtr1 = &cart_ram1[0x3000];
			}
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[0x8000|addr])<<8;
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_atarimax_128k() {
	// atarimax 128k
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	uint32_t bank = 0;
	unsigned char *ramPtr;
	while (1)
	{
		// select the right SRAM block, based on the cartridge bank
		if (bank & 0x8) ramPtr = &cart_ram2[0]; else ramPtr = &cart_ram1[0];
		ramPtr += 8192 * (bank & 0x7);
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			if ((addr & 0xE0) == 0) {
				bank = addr & 0xF;
				if (addr & 0x10) {
					RD5_LOW
					GREEN_LED_OFF
				}
				else {
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_williams() {
	// williams 32k, 64k
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	uint32_t bank = 0;
	unsigned char *bankPtr;
	while (1)
	{
		// select the right SRAM block, based on the cartridge bank
		bankPtr = &cart_ram1[0] + (8192*bank);
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(bankPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			if ((addr & 0xF0) == 0) {
				bank = addr & 0x07;
				if (addr & 0x08) {
					RD5_LOW
					GREEN_LED_OFF
				}
				else {
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_OSS_B() {
	// OSS type B
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	uint32_t bank = 1;
	unsigned char *bankPtr;
	while (1)
	{
		// select the right SRAM block, based on the cartridge bank
		bankPtr = &cart_ram1[0] + (4096*bank);
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			if (addr & 0x1000)
				DATA_OUT = ((uint16_t)cart_ram1[addr&0xFFF])<<8;
			else
				DATA_OUT = ((uint16_t)(*(bankPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			int a0 = addr & 1, a3 = addr & 8;
			if (a3 && !a0) {
				RD5_LOW
				GREEN_LED_OFF
			}
			else {
				RD5_HIGH
				GREEN_LED_ON
				if (!a3 && !a0) bank = 1;
				else if (!a3 && a0) bank = 3;
				else if (a3 && a0) bank = 2;
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_OSS_A(char is034M) {
	// OSS type A (034M, 043M)
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	uint32_t bank = 0;
	unsigned char *bankPtr;
	while (1)
	{
		// select the right SRAM block, based on the cartridge bank
		bankPtr = &cart_ram1[0] + (4096*bank);
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			if (addr & 0x1000)
				DATA_OUT = ((uint16_t)cart_ram1[addr|0x2000])<<8;	// 4k bank #3 always mapped to $Bxxx
			else
				DATA_OUT = ((uint16_t)(*(bankPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN & 0xF;
			if (addr & 0x8) {
				RD5_LOW
				GREEN_LED_OFF
			}
			else {
				RD5_HIGH
				GREEN_LED_ON
				if (addr == 0x0) bank = 0;
				if (addr == 0x3 || addr == 0x7) bank = is034M ? 1 : 2;
				if (addr == 0x4) bank = is034M ? 2 : 1;
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_megacart(int size) {
	// 16k - 128k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, data, c;
	uint32_t bank_mask = 0x00;
	if (size == 32) bank_mask = 0x1;
	else if (size == 64) bank_mask = 0x3;
	else if (size == 128) bank_mask = 0x7;

	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+(addr|0x2000))))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & CCTL) && !(c & RW)) {
			// CCTL low + write
			data = DATA_IN;
			// read the data bus on falling edge of phi2
			while (CONTROL_IN & PHI2)
				data = DATA_IN;
			// new bank is the low n bits written to $D5xx
			int bank = (data>>8) & bank_mask;
			if (bank & 0x4) ramPtr = &cart_ram2[0]; else ramPtr = &cart_ram1[0];
			ramPtr += 16384 * (bank&0x3);
			if (data & 0x8000) {
				RD4_LOW
				RD5_LOW
				GREEN_LED_OFF
			} else {
				RD4_HIGH
				RD5_HIGH
				GREEN_LED_ON
			}
		}
	}
}

void emulate_SIC() {
	// 128k
	RD5_HIGH
	RD4_LOW
	uint16_t addr, data, c;
	uint8_t SIC_byte = 0;
	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+(addr|0x2000))))<<8;
			// wait for phi2 low
			while (CONTROL_IN & PHI2) ;
			SET_DATA_MODE_IN
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			if ((addr & 0xE0) == 0) {
				if (c & RW) {
					// read from $D5xx
					SET_DATA_MODE_OUT
					DATA_OUT = ((uint16_t)SIC_byte)<<8;
					// wait for phi2 low
					while (CONTROL_IN & PHI2) ;
					SET_DATA_MODE_IN
				}
				else {
					// write to $D5xx
					data = DATA_IN;
					// read the data bus on falling edge of phi2
					while (CONTROL_IN & PHI2)
						data = DATA_IN;
					SIC_byte = (uint8_t)(data>>8);
					// switch bank
					if (SIC_byte & 0x4) ramPtr = &cart_ram2[0]; else ramPtr = &cart_ram1[0];
					ramPtr += 16384 * (SIC_byte&0x3);
					if (SIC_byte & 0x40) RD5_LOW else RD5_HIGH
					if (SIC_byte & 0x20) RD4_HIGH else RD4_LOW
					if (SIC_byte == 0x40) GREEN_LED_OFF else GREEN_LED_ON
				}
			}
		}
	}
}

void emulate_SDX(int size) {
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			if ((addr & 0xF0) == 0xE0) {
				// 64k & 128k versions
				if (size == 64) ramPtr = &cart_ram1[0]; else ramPtr = &cart_ram2[0];
				ramPtr += ((~addr) & 0x7) * 8192;
				if (addr & 0x8) {
					RD5_LOW
					GREEN_LED_OFF
				}
				else {
					RD5_HIGH
					GREEN_LED_ON
				}
			}
			if (size == 128 && (addr & 0xF0) == 0xF0) {
				// 128k version only
				ramPtr = &cart_ram1[0] + ((~addr) & 0x7) * 8192;
				if (addr & 0x8) {
					RD5_LOW
					GREEN_LED_OFF
				}
				else {
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_diamond_express(uint8_t cctlAddr) {
	RD5_HIGH
	RD4_LOW
	uint16_t addr, c;
	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)(*(ramPtr+addr)))<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL low
			addr = ADDR_IN;
			if ((addr & 0xF0) == cctlAddr) {
				ramPtr = &cart_ram1[0] + ((~addr) & 0x7) * 8192;
				if (addr & 0x8) {
					RD5_LOW
					GREEN_LED_OFF
				}
				else {
					RD5_HIGH
					GREEN_LED_ON
				}
			}
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_blizzard() {
	//16k
	RD4_HIGH
	RD5_HIGH
	uint16_t addr, c;
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;

		if (!(c & S4)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[addr])<<8;
		}
		else if (!(c & S5)) {
			SET_DATA_MODE_OUT
			addr = ADDR_IN;
			DATA_OUT = ((uint16_t)cart_ram1[0x2000|addr])<<8;
		}
		else if (!(c & CCTL)) {
			// CCTL
			RD4_LOW
			RD5_LOW
			GREEN_LED_OFF
		}
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void feed_XEX_loader(void) {
	RD5_LOW
	RD4_LOW
	GREEN_LED_OFF
	uint16_t addr, data, c;
	uint32_t bank = 0;
	unsigned char *ramPtr = &cart_ram1[0];
	while (1)
	{
		// wait for phi2 high
		while (!((c = CONTROL_IN) & PHI2)) ;
		if (!(c & CCTL)) {
			// CCTL low
			if (c & RW) {
				// read
				SET_DATA_MODE_OUT
				addr = ADDR_IN & 0xFF;
				DATA_OUT = ((uint16_t)ramPtr[addr&0xFF])<<8;
				GREEN_LED_ON
			}
			else {
				// write
				addr = ADDR_IN & 0xFF;
				data = DATA_IN;
				while (CONTROL_IN & PHI2)
					data = DATA_IN;
				if (addr == 0)
					bank = (bank&0xFF00) | (data>>8);
				else if (addr == 1)
					bank = (bank&0x00FF) | (data & 0xFF00);

				if (bank & 0xFF00) ramPtr = &cart_ram2[0]; else ramPtr = &cart_ram1[0];
				ramPtr += 256 * (bank & 0x00FF);
			}
		}
		else
			GREEN_LED_OFF
		// wait for phi2 low
		while (CONTROL_IN & PHI2) ;
		SET_DATA_MODE_IN
	}
}

void emulate_cartridge(int cartType) {
	__disable_irq();	// Disable interrupts

	if (cartType == CART_TYPE_8K) emulate_standard_8k();
	else if (cartType == CART_TYPE_16K) emulate_standard_16k();
	else if (cartType == CART_TYPE_XEGS_32K) emulate_XEGS_32k(0);
	else if (cartType == CART_TYPE_XEGS_64K) emulate_XEGS_64k(0);
	else if (cartType == CART_TYPE_XEGS_128K) emulate_XEGS_128k(0);
	else if (cartType == CART_TYPE_SW_XEGS_32K) emulate_XEGS_32k(1);
	else if (cartType == CART_TYPE_SW_XEGS_64K) emulate_XEGS_64k(1);
	else if (cartType == CART_TYPE_SW_XEGS_128K) emulate_XEGS_128k(1);
	else if (cartType == CART_TYPE_BOUNTY_BOB) emulate_bounty_bob();
	else if (cartType == CART_TYPE_ATARIMAX_1MBIT) emulate_atarimax_128k();
	else if (cartType == CART_TYPE_WILLIAMS_64K) emulate_williams();
	else if (cartType == CART_TYPE_OSS_16K_TYPE_B) emulate_OSS_B();
	else if (cartType == CART_TYPE_OSS_8K) emulate_OSS_B();
	else if (cartType == CART_TYPE_OSS_16K_034M) emulate_OSS_A(1);
	else if (cartType == CART_TYPE_OSS_16K_043M) emulate_OSS_A(0);
	else if (cartType == CART_TYPE_MEGACART_16K) emulate_megacart(16);
	else if (cartType == CART_TYPE_MEGACART_32K) emulate_megacart(32);
	else if (cartType == CART_TYPE_MEGACART_64K) emulate_megacart(64);
	else if (cartType == CART_TYPE_MEGACART_128K) emulate_megacart(128);
	else if (cartType == CART_TYPE_SIC_128K) emulate_SIC();
	else if (cartType == CART_TYPE_SDX_64K) emulate_SDX(64);
	else if (cartType == CART_TYPE_SDX_128K) emulate_SDX(128);
	else if (cartType == CART_TYPE_DIAMOND_64K) emulate_diamond_express(0xD0);
	else if (cartType == CART_TYPE_EXPRESS_64K) emulate_diamond_express(0x70);
	else if (cartType == CART_TYPE_BLIZZARD_16K) emulate_blizzard();
	else if (cartType == CART_TYPE_XEX) feed_XEX_loader();
	else
	{	// no cartridge (cartType = 0)
		GREEN_LED_OFF
		RD4_LOW
		RD5_LOW
		while (1) ;
	}
}

int main(void) {
	/* Ouptut: LEDS - PB{0..1}, RD5 - PB2, RD4 - PB4 */
	config_gpio_leds_RD45();
	/* InOut: Data - PE{8..15} */
	config_gpio_data();
	/* In: Address - PD{0..15} */
	config_gpio_addr();
	/* In: Other Cart Input Sigs - PC{0..2, 4..5} */
	config_gpio_sig();

	RED_LED_ON
	int cartType = 0, atrMode = 0;
	char curPath[256] = "";
	char path[256];
	init();

	while (1) {
		GREEN_LED_OFF

		int cmd = emulate_boot_rom(atrMode);

		GREEN_LED_ON

		// OPEN ITEM n
		if (cmd == CART_CMD_OPEN_ITEM)
		{
			int n = cart_d5xx[0x00];
			DIR_ENTRY *entry = (DIR_ENTRY *)&cart_ram1[0];
			if (entry[n].isDir)
			{	// directory
				strcat(curPath, "/");
				strcat(curPath, entry[n].filename);
				cart_d5xx[0x01] = 0; // path changed
			}
			else
			{	// file/search result
				if (entry[n].full_path[0])
					strcpy(path, entry[n].full_path);	// search result
				else
					strcpy(path, curPath); // file in current directory
				strcat(path, "/");
				strcat(path, entry[n].filename);
				if (stricmp(get_filename_ext(entry[n].filename), "ATR")==0)
				{	// ATR
					cart_d5xx[0x01] = 3;	// ATR
					cartType = CART_TYPE_ATR;
				}
				else
				{	// ROM,CAR or XEX
					cartType = load_file(path);
					if (cartType)
						cart_d5xx[0x01] = (cartType != CART_TYPE_XEX ? 1 : 2);	// file was loaded
					else
					{
						cart_d5xx[0x01] = 4;	// error
						strcpy((char*)&cart_d5xx[0x02], errorBuf);
					}
				}
			}
		}
		// READ DIR
		else if (cmd == CART_CMD_READ_CUR_DIR)
		{
			int ret = read_directory(curPath);
			if (ret) {
				cart_d5xx[0x01] = 0;	// ok
				cart_d5xx[0x02] = num_dir_entries;
			}
			else
			{
				cart_d5xx[0x01] = 1;	// error
				strcpy((char*)&cart_d5xx[0x02], errorBuf);
			}
		}
		// GET DIR ENTRY n
		else if (cmd == CART_CMD_GET_DIR_ENTRY)
		{
			int n = cart_d5xx[0x00];
			DIR_ENTRY *entry = (DIR_ENTRY *)&cart_ram1[0];
			cart_d5xx[0x01] = entry[n].isDir;
			strcpy((char*)&cart_d5xx[0x02], entry[n].long_filename);
		}
		// UP A DIRECTORY LEVEL
		else if (cmd == CART_CMD_UP_DIR)
		{
			int len = strlen(curPath);
			while (len && curPath[--len] != '/');
			curPath[len] = 0;
		}
		// ROOT DIR (when atari reset pressed)
		else if (cmd == CART_CMD_ROOT_DIR)
			curPath[0] = 0;
		// SEARCH str
		else if (cmd == CART_CMD_SEARCH)
		{
			char searchStr[32];
			strcpy(searchStr, (char*)&cart_d5xx[0x00]);
			int ret = search_directory(curPath, searchStr);
			if (ret) {
				cart_d5xx[0x01] = 0;	// ok
				cart_d5xx[0x02] = num_dir_entries;
			}
			else
			{
				cart_d5xx[0x01] = 1;	// error
				strcpy((char*)&cart_d5xx[0x02], errorBuf);
			}
		}
		else if (cmd == CART_CMD_LOAD_SOFT_OS)
		{
			int ret = load_file("UNO_OS.ROM");
			if (!ret) {
				for (int i=0; i<16384; i++)
					cart_ram1[i] = os_rom[i];
			}
			cart_d5xx[0x01] = 0;	// ok
		}
		else if (cmd == CART_CMD_SOFT_OS_CHUNK)
		{
			int n = cart_d5xx[0x00];
			for (int i=0; i<128; i++)
				cart_d5xx[0x01+i]=cart_ram1[n*128+i];
		}
		else if (cmd == CART_CMD_READ_ATR_SECTOR)
		{
			//uint8_t device = cart_d5xx[0x00];
			uint16_t sector = (cart_d5xx[0x02] << 8) | cart_d5xx[0x01];
			uint8_t offset = cart_d5xx[0x03];	// 0 = first 128 byte "page", 1 = second, etc
			int ret = read_atr_sector(sector, offset, &cart_d5xx[0x02]);
			cart_d5xx[0x01] = ret;
		}
		else if (cmd == CART_CMD_WRITE_ATR_SECTOR)
		{
			//uint8_t device = cart_d5xx[0x00];
			GREEN_LED_OFF
			RED_LED_ON
			uint16_t sector = (cart_d5xx[0x02] << 8) | cart_d5xx[0x01];
			uint8_t offset = cart_d5xx[0x03];	// 0 = first 128 byte "page", 1 = second, etc
			int ret = write_atr_sector(sector, offset, &cart_d5xx[0x04]);
			cart_d5xx[0x01] = ret;
			RED_LED_OFF
		}
		else if (cmd == CART_CMD_ATR_HEADER)
		{
			//uint8_t device = cart_d5xx[0x00];
			if (!&mountedATRs[0].path[0])
				cart_d5xx[0x01] = 1;
			else
			{
				memcpy(&cart_d5xx[0x02], &mountedATRs[0].atrHeader, 16);
				cart_d5xx[0x01] = 0;
			}
		}
		// NO CART
		else if (cmd == CART_CMD_NO_CART)
			cartType = 0;
		// REBOOT TO CART
		else if (cmd == CART_CMD_ACTIVATE_CART)
		{
			if (cartType == CART_TYPE_ATR) {
				atrMode = 1;
				RED_LED_OFF
				int ret = mount_atr(path);
				if (ret == 0)
					memcpy(&cart_d5xx[0x02], &mountedATRs[0].atrHeader, 16);
				cart_d5xx[0x01] = ret;
			}
			else
				emulate_cartridge(cartType);
		}
	}

}
