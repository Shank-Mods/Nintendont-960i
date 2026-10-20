/*
	960i experiments: write AVE-RVL (Wii A/V encoder) registers from a file.

	If /nin960i_ave.txt exists on the boot device, every line of the form
	"RR=VV" (hex register and value, e.g. "00=01") is written to the A/V
	encoder right before the game starts. Lines starting with # are
	ignored. Used to find encoder settings that make it follow the 960i
	field timing instead of generating 480p sync on its own.

	The I2C bit-banging is a copy of libogc's (video.c), where it is static.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation version 2.
*/
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "exi.h"
#include "ff_utf8.h"

static vu32* const _i2cReg = (u32*)0xCD800000;
static u32 i2cIdentFlag = 1;

static void ave_udelay(u32 us)
{
	u64 start = gettime();
	while (diff_usec(start, gettime()) < us);
}

static inline void __viOpenI2C(u32 channel)
{
	u32 val = ((_i2cReg[49]&~0x8000)|0x4000);
	val |= _SHIFTL(channel,15,1);
	_i2cReg[49] = val;
}

static inline void __viSetSCL(u32 channel)
{
	u32 val = (_i2cReg[48]&~0x4000);
	val |= _SHIFTL(channel,14,1);
	_i2cReg[48] = val;
}

static inline void __viSetSDA(u32 channel)
{
	u32 val = (_i2cReg[48]&~0x8000);
	val |= _SHIFTL(channel,15,1);
	_i2cReg[48] = val;
}

static inline u32 __viGetSDA(void)
{
	return _SHIFTR(_i2cReg[50],15,1);
}

static void __viCheckI2C(void)
{
	__viOpenI2C(0);
	ave_udelay(4);

	i2cIdentFlag = 0;
	if(__viGetSDA()!=0) i2cIdentFlag = 1;
}

static u32 __sendSlaveAddress(u8 addr)
{
	u32 i;

	__viSetSDA(i2cIdentFlag^1);
	ave_udelay(2);

	__viSetSCL(0);
	for(i=0;i<8;i++) {
		if(addr&0x80) __viSetSDA(i2cIdentFlag);
		else __viSetSDA(i2cIdentFlag^1);
		ave_udelay(2);

		__viSetSCL(1);
		ave_udelay(2);

		__viSetSCL(0);
		addr <<= 1;
	}

	__viOpenI2C(0);
	ave_udelay(2);

	__viSetSCL(1);
	ave_udelay(2);

	if(i2cIdentFlag==1 && __viGetSDA()!=0) return 0;

	__viSetSDA(i2cIdentFlag^1);
	__viOpenI2C(1);
	__viSetSCL(0);

	return 1;
}

static u32 __VISendI2CData(u8 addr,void *val,u32 len)
{
	u8 c;
	s32 i,j;
	u32 level,ret;

	_CPU_ISR_Disable(level);

	__viOpenI2C(1);
	__viSetSCL(1);

	__viSetSDA(i2cIdentFlag);
	ave_udelay(4);

	ret = __sendSlaveAddress(addr);
	if(ret==0) {
		_CPU_ISR_Restore(level);
		return 0;
	}

	__viOpenI2C(1);
	for(i=0;i<len;i++) {
		c = ((u8*)val)[i];
		for(j=0;j<8;j++) {
			if(c&0x80) __viSetSDA(i2cIdentFlag);
			else __viSetSDA(i2cIdentFlag^1);
			ave_udelay(2);

			__viSetSCL(1);
			ave_udelay(2);
			__viSetSCL(0);

			c <<= 1;
		}
		__viOpenI2C(0);
		ave_udelay(2);
		__viSetSCL(1);
		ave_udelay(2);

		if(i2cIdentFlag==1 && __viGetSDA()!=0) {
			_CPU_ISR_Restore(level);
			return 0;
		}

		__viSetSDA(i2cIdentFlag^1);
		__viOpenI2C(1);
		__viSetSCL(0);
	}

	__viOpenI2C(1);
	__viSetSDA(i2cIdentFlag^1);
	ave_udelay(2);
	__viSetSDA(i2cIdentFlag);

	_CPU_ISR_Restore(level);
	return 1;
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Parses two hex digits at s, returns -1 if invalid. */
static int hexbyte(const char *s)
{
	int hi = hexval(s[0]), lo = hexval(s[1]);
	if (hi < 0 || lo < 0) return -1;
	return (hi << 4) | lo;
}

static u8 ave_regs[64][2];
static int ave_count = 0;

/**
 * Read /nin960i_ave.txt, if present. Call while the boot device is mounted.
 * @return Number of register writes found.
 */
int AVE960iLoadFile(void)
{
	char path[32];
	static char buf[8192];	/* the whole file must fit */
	char line[64];
	FIL f;
	UINT read = 0;
	u32 i, l = 0;

	ave_count = 0;
#ifndef NIN960I_EXPERIMENTS
	/* Release/test builds don't touch the A/V encoder */
	(void)path; (void)buf; (void)line; (void)f; (void)read; (void)i; (void)l;
	return 0;
#endif
	snprintf(path, sizeof(path), "%s:/nin960i_ave.txt", GetRootDevice());
	if (f_open_char(&f, path, FA_READ|FA_OPEN_EXISTING) != FR_OK)
		return 0;
	f_read(&f, buf, sizeof(buf) - 1, &read);
	f_close(&f);
	buf[read] = '\n';

	for (i = 0; i <= read; i++) {
		char c = buf[i];
		if (c != '\n' && c != '\r') {
			if (l < sizeof(line) - 1) line[l++] = c;
			continue;
		}
		line[l] = 0;
		if (l >= 5 && line[0] != '#' && (line[2] == '=' || line[2] == ' ') && ave_count < 64) {
			int reg = hexbyte(line), val = hexbyte(line + 3);
			if (reg >= 0 && val >= 0) {
				ave_regs[ave_count][0] = (u8)reg;
				ave_regs[ave_count][1] = (u8)val;
				ave_count++;
			}
		}
		l = 0;
	}
	return ave_count;
}

/**
 * Write the registers read by AVE960iLoadFile() to the A/V encoder.
 * Call after the game's video mode has been set.
 * @return Number of registers written.
 */
int AVE960iApply(void)
{
	int i, written = 0;

	if (ave_count == 0)
		return 0;
	__viCheckI2C();
	for (i = 0; i < ave_count; i++) {
		if (__VISendI2CData(0xe0, ave_regs[i], 2))
			written++;
		gprintf("AVE960i: reg %02X = %02X\r\n", ave_regs[i][0], ave_regs[i][1]);
		ave_udelay(2);
	}
	return written;
}
