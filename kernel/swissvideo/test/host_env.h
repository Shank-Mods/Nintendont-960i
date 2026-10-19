/*
	Minimal stand-in for the kernel environment, so SwissVideo.c can be
	built as a normal PowerPC Linux program and run under qemu-ppc.
	See swissvideo_test.c.
*/
#ifndef __SWISSVIDEO_HOST_ENV_H__
#define __SWISSVIDEO_HOST_ENV_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
typedef int bool;
#define true 1
#define false 0

/* MEM1 image lives here in the test process */
#define MEM1_BASE 0x30000000

#include "../../../common/include/CommonConfig.h"

extern u32 BI2region;
extern u32 test_game_id;

static inline bool ConfigGetConfig(u32 c) { return (c & NIN_CFG_960I) != 0; }
static inline u32 ConfigGetVideoMode(void) { return NIN_VID_PROG; }
static inline u32 ConfigGetGameID(void) { return test_game_id; }
static inline bool IsWiiU(void) { return false; }

static inline void sync_after_write(void *p, int l) { (void)p; (void)l; }
static inline void sync_before_read(void *p, int l) { (void)p; (void)l; }

#define dbgprintf printf

#endif
