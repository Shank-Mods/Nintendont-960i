/*
	Host test harness for the Swiss video patcher (kernel/SwissVideo.c).

	Loads a GameCube main.dol into an emulated MEM1 exactly where the game
	would run, runs the same patcher the Nintendont kernel runs, then prints
	what was found and every word that changed, plus the installed stubs.

	Build and run (big endian PowerPC, e.g. under qemu-ppc):
		make -C kernel/swissvideo/test
		qemu-ppc kernel/swissvideo/test/swissvideo_test main.dol GALE [P|N] [name=value ...]

	"P" marks a PAL disc (BI2 region). The settings default to what the
	loader passes for 960i with everything else on Auto; name=value pairs
	(decimal, Swiss numbering) change them:
		vmode ave dtv rt4k active hscale voffset vfilter vjitter
		pixel dither aniso wide prog 60hz
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "host_env.h"
#include "../../SwissVideo.h"

u32 BI2region = 0;
u32 test_game_id = 0;
SWISS_BOOT test_swiss_boot;

static void set_option(const char *opt)
{
	char name[16];
	int v;
	if (sscanf(opt, "%15[^=]=%d", name, &v) != 2) {
		fprintf(stderr, "bad option %s\n", opt);
		exit(1);
	}
	SWISS_BOOT *b = &test_swiss_boot;
	if (!strcmp(name, "vmode")) b->gameVMode = v;
	else if (!strcmp(name, "ave")) b->aveCompat = v;
	else if (!strcmp(name, "dtv")) b->forceDTVStatus = v;
	else if (!strcmp(name, "rt4k")) b->rt4kOptim = v;
	else if (!strcmp(name, "active")) b->forceVideoActive = v;
	else if (!strcmp(name, "hscale")) b->forceHScale = v;
	else if (!strcmp(name, "voffset")) b->forceVOffset = v;
	else if (!strcmp(name, "vfilter")) b->forceVFilter = v;
	else if (!strcmp(name, "vjitter")) b->forceVJitter = v;
	else if (!strcmp(name, "pixel")) b->fixPixelCenter = v;
	else if (!strcmp(name, "dither")) b->disableDithering = v;
	else if (!strcmp(name, "aniso")) b->forceAnisotropy = v;
	else if (!strcmp(name, "wide")) b->forceWidescreen = v;
	else if (!strcmp(name, "prog")) b->sramProgressive = v;
	else if (!strcmp(name, "60hz")) b->sram60Hz = v;
	else { fprintf(stderr, "unknown option %s\n", name); exit(1); }
}

#define MEM1_SIZE	0x01800000
#define PATCH_TOP	0x2FA0	/* roughly where Nintendont's patch area starts */

static u32 poffset = PATCH_TOP;

u32 PatchCopy(const u8 *PatchPtr, const u32 PatchSize)
{
	poffset -= PatchSize;
	memcpy((void *)(MEM1_BASE + poffset), PatchPtr, PatchSize);
	return MEM1_BASE + poffset;
}

typedef struct {
	u32 offsetText[7], offsetData[11];
	u32 addressText[7], addressData[11];
	u32 sizeText[7], sizeData[11];
	u32 addressBSS, sizeBSS, entrypoint;
} dolhdr;

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s main.dol GAMEID4 [P|N] [name=value ...]\n", argv[0]);
		return 1;
	}
	if (mmap((void *)MEM1_BASE, MEM1_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)MEM1_BASE) {
		perror("mmap");
		return 1;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	rewind(f);
	u8 *file = malloc(fsize);
	if (fread(file, 1, fsize, f) != (size_t)fsize) { perror("read"); return 1; }
	fclose(f);

	memcpy(&test_game_id, argv[2], 4);	/* big endian host: matches ConfigGetGameID */
	BI2region = (argc > 3 && argv[3][0] == 'P') ? BI2_REGION_PAL : BI2_REGION_USA;

	/* 960i, AVE-RVL, everything else Auto (what the loader works out) */
	memset(&test_swiss_boot, 0, sizeof(test_swiss_boot));
	test_swiss_boot.Magic = SWISS_BOOT_MAGIC;
	test_swiss_boot.gameVMode = 4;
	test_swiss_boot.aveCompat = 5;
	test_swiss_boot.forceVFilter = 1;
	test_swiss_boot.forceVJitter = 1;
	test_swiss_boot.sramVideo = (BI2region == BI2_REGION_PAL) ? 1 : 0;
	test_swiss_boot.sramProgressive = 1;
	test_swiss_boot.sram60Hz = (BI2region == BI2_REGION_PAL) ? 1 : 0;
	for (int a = 3; a < argc; a++)
		if (strchr(argv[a], '='))
			set_option(argv[a]);

	dolhdr *dol = (dolhdr *)file;
	u32 minoff = 0x81800000, maxoff = 0, i;
	for (i = 0; i < 18; i++) {
		u32 addr = i < 7 ? dol->addressText[i] : dol->addressData[i - 7];
		u32 size = i < 7 ? dol->sizeText[i] : dol->sizeData[i - 7];
		u32 off  = i < 7 ? dol->offsetText[i] : dol->offsetData[i - 7];
		if (!addr || !size)
			continue;
		memcpy((void *)(MEM1_BASE + (addr & 0x01FFFFFF)), file + off, size);
		if (addr < minoff) minoff = addr;
		if (addr + size > maxoff) maxoff = addr + size;
	}
	minoff &= 0x01FFFFFF;
	maxoff &= 0x01FFFFFF;
	printf("DOL MinOff:0x%08X MaxOff:0x%08X\n", minoff, maxoff);

	/* keep a copy to report changes */
	u32 len = maxoff - minoff;
	u8 *orig = malloc(len);
	memcpy(orig, (void *)(MEM1_BASE + minoff), len);

	SwissVideoPatch((char *)(MEM1_BASE + minoff), len);

	u32 changed = 0;
	printf("\n--- changed words ---\n");
	for (i = 0; i < len; i += 4) {
		u32 o = *(u32 *)(orig + i), n = *(u32 *)(MEM1_BASE + minoff + i);
		if (o != n) {
			printf("%08X: %08X -> %08X\n", 0x80000000 | (minoff + i), o, n);
			changed++;
		}
	}
	printf("%u words changed\n", changed);

	printf("\n--- installed stubs 0x%08X-0x%08X ---\n", 0x80000000 | poffset, 0x80000000 | PATCH_TOP);
	for (i = poffset; i < PATCH_TOP; i += 4)
		printf("%08X: %08X\n", 0x80000000 | i, *(u32 *)(MEM1_BASE + i));

	printf("\n--- variables ---\n");
	for (i = 0x9E0; i < 0xA00; i++)
		printf("%02X%s", *(u8 *)(MEM1_BASE + i), (i & 15) == 15 ? "\n" : " ");
	return 0;
}
