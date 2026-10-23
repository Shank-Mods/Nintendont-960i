/*
	1280x240p two-pass experiment: layout shared by the kernel (installer,
	SwissVideo.c) and the PowerPC payload (hires.c / hires_entry.S).
	Nintendont addition, not part of Swiss.
*/
#ifndef __HIRES_SHARED_H__
#define __HIRES_SHARED_H__

/* Low memory word (0x80000000 + offset) holding the hr_data address */
#define HR_DATA_PTR_VAR		0x09C0

/* Statistics, uncached MEM2 (PPC) / ARM view; 0x80 bytes after the VI snapshot */
#define HR_STATS_PPC		0xD3003280
#define HR_STATS_ARM		0x13003280
#define HR_STATS_MAGIC		0x48524553	/* 'HRES' */

/* Offsets of the entry points in the payload */
#define HR_ENTRY_VIEWPORT	0x00
#define HR_ENTRY_SCISSOR	0x04
#define HR_ENTRY_COPYDISP	0x08

#define HR_MODE_OFF		0
#define HR_MODE_LEFT		1	/* left half only (VI + GX setup test) */
#define HR_MODE_TWOPASS		2	/* render the frame twice */

#define HR_MAX_SCISSOR		32

#ifndef _LANGUAGE_ASSEMBLY

/* Everything the payload keeps between calls (it has no globals). */
struct hr_data {
	unsigned int mode;
	unsigned int tramp_vp[2];	/* GXSetViewport: first instruction, b back */
	unsigned int tramp_sc[2];	/* GXSetScissor */
	unsigned int tramp_cd[2];	/* GXCopyDisp */
	unsigned int gxdata_fn[2];	/* lwz r3, __GXData ; blr */
	unsigned int frame_start;	/* physical FIFO address of this frame, 0 = unknown */
	unsigned int prologue_pos;	/* FIFO write pointer before the frame prologue */
	unsigned int prologue_main;	/* prologue set the full screen scissor */
	unsigned int cur_l, cur_t, cur_w, cur_h, cur_main;	/* last GXSetScissor */
	unsigned int nsc;		/* full screen GXSetScissor calls in this frame */
	unsigned int sc_pos[HR_MAX_SCISSOR];	/* FIFO write pointer before each */
};

struct hr_stats {
	unsigned int magic;
	unsigned int copies;		/* GXCopyDisp calls */
	unsigned int replays;		/* frames rendered twice */
	unsigned int skip_first;	/* no frame start yet */
	unsigned int skip_big;		/* frame bigger than the FIFO allows */
	unsigned int skip_overwritten;	/* frame start overwritten (FIFO wrapped) */
	unsigned int skip_nocopy;	/* display copy not found in the FIFO */
	unsigned int skip_timeout1;	/* GPU didn't finish the first pass */
	unsigned int timeout2;		/* GPU didn't finish the second pass */
	unsigned int sc_lost;		/* recorded scissor not found again */
	unsigned int gxdata_ok;
	unsigned int gxdata_bad;	/* __GXData copy registers not where expected */
	unsigned int fifo_base;
	unsigned int fifo_size;
	unsigned int max_frame;		/* largest frame, bytes */
	unsigned int last_frame;
	unsigned int sc_calls;		/* GXSetScissor calls */
	unsigned int sc_main;		/* ... of them full screen */
	unsigned int last_sc_lt;	/* left << 16 | top */
	unsigned int last_sc_wh;	/* width << 16 | height */
	unsigned int vp_w;		/* last GXSetViewport width / height (float bits) */
	unsigned int vp_h;
	unsigned int last_patched;	/* scissors patched for the second pass */
	unsigned int last_disp_size;	/* __GXData copy size register seen */
	unsigned int reserved[8];
};

#endif
#endif
