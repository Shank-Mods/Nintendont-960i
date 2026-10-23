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

/*
	Game state snapshot taken by PADRead (loader/source/ppc/PADReadGC) for
	the log: [0] current thread, [1] thread that draws (main_thread),
	[2] its saved srr0, [3] its saved lr (when it isn't the one running
	PADRead), [6] CP status << 16 | control,
	[7] CP distance, [8] CP read pointer, [9] CP write pointer,
	[10] that thread's state << 16 | suspend count.
*/
#define HR_GAMESNAP_PPC		0xD3003300
#define HR_GAMESNAP_ARM		0x13003300

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
	unsigned int jmp[4][4];		/* lis r12 / ori r12 / mtctr r12 / bctr to the payload */
};

/*
	The payload runs from MEM2 (the PPC can execute it there, like PADReadGC
	at 0x93000000); the patch area in low MEM1 is too small for it. Hooks
	branch to jmp[] in hr_data (MEM1), which jumps on with an absolute
	address.
*/
#define HR_PAYLOAD_MEM2_PPC	0x93170000
#define HR_PAYLOAD_MEM2_ARM	0x13170000
#define HR_PAYLOAD_MEM2_MAX	0x8000

#define HR_TEST_SAME	1	/* replay the frame unchanged (no right half patching) */
#define HR_TEST_KEEPON	16	/* keep replaying after hung replays (no switch to Left half) */
#define HR_TEST_NOCOPY	32	/* second pass: don't move the copy to the right half */
#define HR_TEST_NOSC	64	/* second pass: don't move scissors / viewports */
#define HR_TEST_REWRITE	128	/* write the copy / scissor commands back unchanged */
/* 256 was "32 bit FIFO writes", now always on */

/* Our PE token values (not the game's, not markers 0xAxxx) */
#define HR_TOKEN_A	0x5A51
#define HR_TOKEN_B	0x5A52


#define HR_MARKER	0xA000
#define HR_MAX_FAILS	4	/* then "Left half" for the rest of the session */

struct hr_stats2 {
	unsigned int dl_inlined;	/* display lists copied into the FIFO */
	unsigned int dl_called;		/* ... called as usual (too big, misaligned) */
	unsigned int dl_bytes;		/* bytes copied */
	unsigned int dl_max;		/* largest list copied */
	unsigned int dl_big;		/* largest list called */
	unsigned int main_thread;	/* OSThread that calls GXCopyDisp (0x800000E4) */
	unsigned int dist_fixed;	/* CP distance put back to 0 after a replay */
	unsigned int diag_nmk;		/* first timeout: markers in that frame */
	unsigned int recovered;		/* GPU resets after a hung replay */
	unsigned int vp_shifted;	/* viewports moved for second passes */
	unsigned int reserved[5];
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
	unsigned int unused_v0, unused_v1;
	unsigned int diag_where;	/* first timeout: 1 first pass, 2 second pass */
	unsigned int diag_sr;		/* CP status << 16 | CP control */
	unsigned int diag_cnt;		/* CP read/write distance */
	unsigned int diag_rp;		/* CP read pointer - frame start */
	unsigned int diag_tok;		/* PE token seen << 16 | token waited for */
};

#endif
#endif
