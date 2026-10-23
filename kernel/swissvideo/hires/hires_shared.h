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

/* FIFO bytes around the CP read pointer at the first timeout (0x80 bytes) */
#define HR_DUMP_PPC		0xD3003300
#define HR_DUMP_ARM		0x13003300
#define HR_DUMP_WORDS		30	/* + address and length */

/* More counters (0x40 bytes) */
#define HR_STATS2_PPC		0xD3003380
#define HR_STATS2_ARM		0x13003380

/* Display lists up to this size are copied into the FIFO instead of called */
#define HR_DL_INLINE_MAX	16384

/* Offsets of the entry points in the payload */
#define HR_ENTRY_VIEWPORT	0x00
#define HR_ENTRY_SCISSOR	0x04
#define HR_ENTRY_COPYDISP	0x08
#define HR_ENTRY_CALLDL		0x0C

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
	unsigned int tramp_dl[2];	/* GXCallDisplayList */
	unsigned int nop_dl[16];	/* 32 aligned bytes of NOPs inside */
	unsigned int nmk;		/* markers (PE tokens 0xA000 + n) in this frame */
	unsigned int fails;		/* replays that hung the GPU */
	unsigned int test;		/* HR_TEST_* (experiments: hrtest= in nin960i_vi.txt) */
};

#define HR_TEST_SAME	1	/* replay the frame unchanged (no right half patching) */
#define HR_TEST_IDLE	2	/* also wait for the CP idle bits before rewinding */
#define HR_TEST_DELAY	4	/* wait 1 ms before rewinding */
#define HR_TEST_VTX	8	/* re-send the vertex formats in the frame prologue (made
				   replays hang now and then on hardware, off by default) */
#define HR_TEST_KEEPON	16	/* keep replaying after hung replays (no switch to Left half) */

/* Our PE token values (not the game's, not markers 0xAxxx) */
#define HR_TOKEN_A	0x5A51
#define HR_TOKEN_B	0x5A52

/* FIFO write pointer at the last 16 markers (marker n at [n & 15]) */
#define HR_MKPOS_PPC		0xD30033C0
#define HR_MKPOS_ARM		0x130033C0

#define HR_MARKER	0xA000
#define HR_MAX_FAILS	4	/* then "Left half" for the rest of the session */

struct hr_stats2 {
	unsigned int dl_inlined;	/* display lists copied into the FIFO */
	unsigned int dl_called;		/* ... called as usual (too big, misaligned) */
	unsigned int dl_bytes;		/* bytes copied */
	unsigned int dl_max;		/* largest list copied */
	unsigned int dl_big;		/* largest list called */
	unsigned int diag_mk;		/* first pass 2 timeout: PE token (marker) */
	unsigned int diag_mk_off;	/* ... its FIFO position - frame start */
	unsigned int diag_nmk;		/* ... markers in that frame */
	unsigned int recovered;		/* GPU resets after a hung replay */
	unsigned int fs[4];		/* first 16 bytes of the frame that hung */
	unsigned int idle_ok;		/* HR_TEST_IDLE: CP idle after the first pass */
	unsigned int idle_to;		/* ... CP not idle within 0.5 s */
	unsigned int hung_frames;	/* replays that hung: frame number bits (copies & 31) */
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
	unsigned int pe_nopped;	/* draw done / token interrupts removed from replays */
	unsigned int vtx_ok;		/* frame prologues with the vertex formats re-sent */
	unsigned int vtx_bad;		/* __GXData vertex formats not where expected */
	unsigned int diag_where;	/* first timeout: 1 first pass, 2 second pass */
	unsigned int diag_sr;		/* CP status << 16 | CP control */
	unsigned int diag_cnt;		/* CP read/write distance */
	unsigned int diag_rp;		/* CP read pointer - frame start */
	unsigned int diag_tok;		/* PE token seen << 16 | token waited for */
};

#endif
#endif
