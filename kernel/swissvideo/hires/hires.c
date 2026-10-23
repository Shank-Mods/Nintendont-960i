/*
	1280x240p two-pass experiment: GX hooks (PowerPC payload).
	Nintendont addition, not part of Swiss.

	The game draws its frame once as usual, but with a 1280x240 viewport and
	the scissor limited to the left 640 columns (the EFB is only 640 wide).
	At GXCopyDisp the left half is copied into the framebuffer (1280 pixels
	per line), then the frame's GPU commands, which are still in the FIFO,
	are run a second time for the right half. The copy at the end of that
	second pass goes to the right half of the framebuffer.

	Shifting the picture by 640 with the scissor offset alone hung the GPU
	(GXSetScissorBoxOffset allows -342..382), so the second pass moves it by
	300 with the viewports (GXSetViewport allows a left edge down to -342)
	and by 340 with the scissor offset.

	Position independent and without globals: all state lives in struct
	hr_data, whose address the kernel stores at 0x80000000 + HR_DATA_PTR_VAR.
	Built with -fno-jump-tables -mno-sdata and checked for relocations.
*/
#include "hires_shared.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define HR	((struct hr_data *)(*(volatile u32 *)(0x80000000 | HR_DATA_PTR_VAR)))
#define ST	((volatile struct hr_stats *)HR_STATS_PPC)
#define ST2	((volatile struct hr_stats2 *)HR_STATS2_PPC)

#define CPR	((volatile u16 *)0xCC000000)	/* command processor */
#define PER	((volatile u16 *)0xCC001000)	/* pixel engine (7 = token) */
#define PIR	((volatile u32 *)0xCC003000)	/* processor interface */
#define WGP8	(*(volatile u8 *)0xCC008000)	/* write gather pipe */
#define WGP32	(*(volatile u32 *)0xCC008000)

#define UNCACHED(p)	((volatile u8 *)(0xC0000000 | (p)))
#define NOTFOUND	0xFFFFFFFF

/*
	Full screen scissor as the GPU sees it (GXSetScissor adds 342 to all
	coordinates), followed by our scissor offset write:
	tile 0: box (0,0)-(639,239), offset 0
	tile 1 (viewports moved left by 300): box (340,0)-(979,239), offset 340
*/
#define BP_SCIS0_T0	0x20156156	/* top 342, left 342 */
#define BP_SCIS1_T0	0x213D5245	/* bottom 581, right 981 */
#define BP_SCOFF_T0	0x5902ACAB	/* (0+342)/2, (0+342)/2 */
#define BP_SCIS0_T1	0x202AA156	/* top 342, left 682 */
#define BP_SCIS1_T1	0x21529245	/* bottom 581, right 1321 */
#define BP_SCOFF_T1	0x5902AD55	/* (340+342)/2, (0+342)/2 */
#define VP_SHIFT_BITS	0x43960000	/* 300.0f */

/* Timeouts in time base ticks (40.5 MHz in GameCube mode): 0.5 s */
#define WAIT_TICKS	20250000

typedef void (*scissor_fn)(u32, u32, u32, u32);
typedef void (*copydisp_fn)(void *, u32);
typedef u32 *(*gxdata_fn)(void);
typedef void (*calldl_fn)(const void *, u32);

static inline void ppc_sync(void)
{
	__asm__ volatile ("sync" : : : "memory");
}

static inline void bp_write(u32 v)
{
	WGP8 = 0x61;
	WGP32 = v;
}

static inline u32 mftb(void)
{
	u32 t;
	__asm__ volatile ("mftb %0" : "=r" (t));
	return t;
}

static inline u32 irq_off(void)
{
	u32 msr;
	__asm__ volatile ("mfmsr %0" : "=r" (msr));
	__asm__ volatile ("mtmsr %0" : : "r" (msr & ~0x8000) : "memory");
	return msr;
}

static inline void irq_restore(u32 msr)
{
	__asm__ volatile ("mtmsr %0" : : "r" (msr) : "memory");
}

static inline void fifo_nops(u32 words)
{
	while (words--)
		WGP32 = 0;
}

/* CPU FIFO write pointer: everything up to here has left the gather pipe. */
static inline u32 fifo_wp(void)
{
	ppc_sync();
	return PIR[5] & 0x03FFFFE0;
}

static inline u32 cp_get32(u32 i)
{
	return ((u32)CPR[i + 1] << 16) | CPR[i];
}

static inline void cp_set32(u32 i, u32 v)
{
	CPR[i] = v & 0xFFFF;
	CPR[i + 1] = v >> 16;
}

struct ring {
	u32 base, size;
};

static inline u32 ring_add(const struct ring *r, u32 p, u32 n)
{
	p += n;
	if (p >= r->base + r->size)
		p -= r->size;
	return p;
}

static inline u32 ring_dist(const struct ring *r, u32 from, u32 to)
{
	return (to >= from) ? to - from : to + r->size - from;
}

static inline u8 ring_rd(const struct ring *r, u32 p, u32 n)
{
	return *UNCACHED(ring_add(r, p, n));
}

/*
	FIFO patches are 32 bit read-modify-write: single byte stores to the
	FIFO made the GPU hang on the patched command, even when the value
	written was the one already there (hardware tests T8 / T9).
*/
static inline void ring_wr(const struct ring *r, u32 p, u32 n, u8 v)
{
	u32 a = ring_add(r, p, n);
	volatile u32 *w = (volatile u32 *)(0xC0000000 | (a & ~3));
	u32 sh = (3 - (a & 3)) * 8;
	*w = (*w & ~(0xFFu << sh)) | ((u32)v << sh);
}

/* Does a BP write "61 rr vv vv vv" with this 32 bit value start at p? */
static int bp_at(const struct ring *r, u32 p, u32 v)
{
	return ring_rd(r, p, 0) == 0x61 &&
		ring_rd(r, p, 1) == (u8)(v >> 24) &&
		ring_rd(r, p, 2) == (u8)(v >> 16) &&
		ring_rd(r, p, 3) == (u8)(v >> 8) &&
		ring_rd(r, p, 4) == (u8)v;
}

static void bp_set(const struct ring *r, u32 p, u32 v)
{
	ring_wr(r, p, 1, v >> 24);
	ring_wr(r, p, 2, v >> 16);
	ring_wr(r, p, 3, v >> 8);
	ring_wr(r, p, 4, v);
}

/* First BP write with value v in [p, p + window), or NOTFOUND. */
static u32 find_bp(const struct ring *r, u32 p, u32 window, u32 v)
{
	u32 i;
	for (i = 0; i < window; i++) {
		if (bp_at(r, ring_add(r, p, i), v))
			return ring_add(r, p, i);
	}
	return NOTFOUND;
}

/* Tile 0 full screen scissor + offset (15 bytes) in [p, p + window). */
static u32 find_scissor(const struct ring *r, u32 p, u32 window)
{
	u32 i, q;
	for (i = 0; i < window; i++) {
		q = ring_add(r, p, i);
		if (bp_at(r, q, BP_SCIS0_T0) &&
		    bp_at(r, ring_add(r, q, 5), BP_SCIS1_T0) &&
		    bp_at(r, ring_add(r, q, 10), BP_SCOFF_T0))
			return q;
	}
	return NOTFOUND;
}

/*
	The replayed range starts right after the previous frame's copy, so it
	holds the game's GXSetDrawDone (BP 0x45) and GXSetDrawSync (BP 0x48)
	for that frame. Run twice, they raise a second "draw done" / token
	interrupt, and the game's callbacks (framebuffer queues) see a frame
	finish twice. Pass 1 has already run them, so they are replaced by
	NOPs (opcode 00) for the replay.
*/

static u32 nop_pe_interrupts(const struct ring *r, u32 p, u32 len, u32 *last_tok)
{
	u32 i, j, q, n = 0;
	u8 reg;

	/* exact values: draw done 45 000002, token interrupt 48 00xxxx */
	for (i = 0; i + 5 <= len; i++) {
		q = ring_add(r, p, i);
		if (ring_rd(r, q, 0) != 0x61)
			continue;
		reg = ring_rd(r, q, 1);
		if (reg == 0x47 && ring_rd(r, q, 2) == 0) {
			/* the game's GXSetDrawSync token (ours are 5A51-5A54) */
			u32 t = ((u32)ring_rd(r, q, 3) << 8) | ring_rd(r, q, 4);
			if ((t & 0xFFF8) != 0x5A50)
				*last_tok = t;
			continue;
		}
		if (reg == 0x45) {
			if (ring_rd(r, q, 2) != 0 || ring_rd(r, q, 3) != 0 || ring_rd(r, q, 4) != 2)
				continue;
		} else if (reg == 0x48) {
			if (ring_rd(r, q, 2) != 0)
				continue;
		} else
			continue;
		for (j = 0; j < 5; j++)
			ring_wr(r, q, j, 0);
		n++;
		i += 4;
	}
	return n;
}

/*
	Second pass: every viewport (XF 0x101A-0x101F, "10 00 05 10 1A" + 6
	floats; the 4th is the x offset) moves 300 pixels left.
*/
static u32 shift_viewports(const struct ring *r, u32 p, u32 len)
{
	union { float f; u32 i; } v, k;
	u32 i, q, n = 0;

	k.i = VP_SHIFT_BITS;		/* no float constants: no data section */
	__asm__ ("" : "+r" (k.i));	/* keep gcc from folding it into .rodata */
	for (i = 0; i + 29 <= len; i++) {
		q = ring_add(r, p, i);
		if (ring_rd(r, q, 0) != 0x10 || ring_rd(r, q, 1) != 0 || ring_rd(r, q, 2) != 5 ||
		    ring_rd(r, q, 3) != 0x10 || ring_rd(r, q, 4) != 0x1A)
			continue;
		v.i = ((u32)ring_rd(r, q, 17) << 24) | ((u32)ring_rd(r, q, 18) << 16) |
		      ((u32)ring_rd(r, q, 19) << 8) | ring_rd(r, q, 20);
		v.f -= k.f;
		ring_wr(r, q, 17, v.i >> 24);
		ring_wr(r, q, 18, v.i >> 16);
		ring_wr(r, q, 19, v.i >> 8);
		ring_wr(r, q, 20, v.i);
		n++;
		i += 28;
	}
	return n;
}

static void set_scissor_tile1(const struct ring *r, u32 q)
{
	bp_set(r, q, BP_SCIS0_T1);
	bp_set(r, ring_add(r, q, 5), BP_SCIS1_T1);
	bp_set(r, ring_add(r, q, 10), BP_SCOFF_T1);
}

/*
	Wait until the GPU has processed everything sent so far: a PE token
	(BP 0x47, no interrupt) is sent and its value polled in the PE token
	register, like GXSetDrawSync / GXReadDrawSync. The CP idle bits alone
	turned out not to be reliable.
*/
static int wait_gpu(struct hr_data *d, u32 tok, u32 where, u32 frame_start)
{
	u32 t0;

	bp_write(0x47000000 | tok);
	fifo_nops(8);			/* push it out of the gather pipe */
	ppc_sync();
	t0 = mftb();
	while (mftb() - t0 < WAIT_TICKS) {
		if (PER[7] == tok)
			return 1;
	}
	if (!ST->diag_where) {		/* keep the first one */
		ST->diag_where = where;
		ST->diag_sr = ((u32)CPR[0] << 16) | CPR[1];
		ST->diag_cnt = cp_get32(24);
		ST->diag_rp = (cp_get32(28) & 0x3FFFFFFF) - frame_start;
		ST->diag_tok = ((u32)PER[7] << 16) | tok;
	}
	return 0;
}

/*
	The replay hung the GPU: reset the GP FIFO path like GXAbortFrame
	(PI reset register 0xCC003018) and point the CP at the current end of
	the FIFO, as __GXCleanGPFifo does, so the game can go on.
*/
static void recover_gpu(u32 en)
{
	u32 pi3 = PIR[3], pi4 = PIR[4], pi5, wp, t0;

	CPR[1] = en & ~0x0D;		/* stop reading, no watermark interrupts */
	ppc_sync();
	pi5 = PIR[5];
	PIR[6] = 1;
	t0 = mftb(); while (mftb() - t0 < 4000) ;
	PIR[6] = 0;
	t0 = mftb(); while (mftb() - t0 < 4000) ;
	PIR[3] = pi3;
	PIR[4] = pi4;
	PIR[5] = pi5;
	wp = pi5 & 0x03FFFFE0;
	cp_set32(24, 0);
	cp_set32(26, wp);
	cp_set32(28, wp);
	ppc_sync();
	CPR[2] = 3;
	CPR[1] = en;
	ST2->recovered++;
}



/*
	The display copy has to copy one 640x240 tile into a framebuffer with
	1280 pixel lines. GXCopyDisp sends the copy registers from __GXData
	(cpDispSrc/Size/Stride at +480/+484/+488 in this SDK family, the same
	layout Swiss' GXSetDispCopyYScale patch uses); the values are checked
	before they are changed.
*/
static void fix_copy_regs(struct hr_data *d)
{
	u32 g = (u32)((gxdata_fn)d->gxdata_fn)();
	volatile u32 *f;

	/* must be a cached MEM1 address, or the lookup is wrong */
	if (g < 0x80000000 || g >= 0x81800000 - 0x400 || (g & 3)) {
		ST->last_disp_size = g;
		ST->gxdata_bad++;
		return;
	}
	f = (volatile u32 *)(g + 480);
	ST->last_disp_size = f[1];
	if ((f[0] >> 24) == 0x49 && (f[1] >> 24) == 0x4A && (f[2] >> 24) == 0x4D) {
		f[0] = 0x49000000;		/* left 0, top 0 */
		f[1] = 0x4A03BE7F;		/* 640 x 240 */
		f[2] = 0x4D000050;		/* 1280 pixel stride */
		ST->gxdata_ok++;
	} else {
		ST->gxdata_bad++;
	}
}

/* Start of a frame: NOPs to mark the boundary, then restore the scissor. */
static void frame_prologue(struct hr_data *d, const struct ring *r)
{
	scissor_fn orig = (scissor_fn)d->tramp_sc;
	u32 p;

	fifo_nops(16);			/* 64 bytes: the last 32 before p are NOPs */
	p = fifo_wp();
	d->frame_start = (p - 32 >= r->base) ? p - 32 : p - 32 + r->size;
	d->nsc = 0;
	d->prologue_pos = p;
	d->prologue_main = d->cur_main;
	if (d->cur_main)
		orig(0, 0, 640, 240);
	else
		orig(d->cur_l, d->cur_t, d->cur_w, d->cur_h);
	bp_write(BP_SCOFF_T0);
}

void hr_scissor(u32 l, u32 t, u32 w, u32 h)
{
	struct hr_data *d = HR;
	scissor_fn orig = (scissor_fn)d->tramp_sc;
	u32 full = (l == 0 && t == 0 &&
		    ((w == 640 && h == 480) || (w == 1280 && h == 240)));

	d->cur_l = l;
	d->cur_t = t;
	d->cur_w = w;
	d->cur_h = h;
	d->cur_main = full;
	ST->sc_calls++;
	ST->last_sc_lt = (l << 16) | (t & 0xFFFF);
	ST->last_sc_wh = (w << 16) | (h & 0xFFFF);

	if (full) {
		ST->sc_main++;
		if (d->nsc < HR_MAX_SCISSOR)
			d->sc_pos[d->nsc++] = fifo_wp();
		orig(0, 0, 640, 240);
	} else {
		orig(l, t, w, h);
	}
	bp_write(BP_SCOFF_T0);
}

void hr_copydisp(void *dest, u32 clear)
{
	struct hr_data *d = HR;
	copydisp_fn orig = (copydisp_fn)d->tramp_cd;
	struct ring r;
	u32 dst = (u32)dest & 0x3FFFFFFF;
	u32 posL, end, dist, at, i, patched, en, tok, wp, msr;
	u32 found[HR_MAX_SCISSOR + 1];
	u32 nfound = 0;

	tok = 0xFFFFFFFF;		/* game token not changed yet */
	r.base = cp_get32(16) & 0x3FFFFFFF;
	r.size = (cp_get32(18) & 0x3FFFFFFF) + 4 - r.base;
	ST->magic = HR_STATS_MAGIC;
	ST->copies++;
	ST2->main_thread = *(volatile u32 *)0x800000E4;
	ST->fifo_base = r.base;
	ST->fifo_size = r.size;

	fix_copy_regs(d);

	/* Pass 1 copy: left half */
	posL = fifo_wp();
	orig(dest, clear);

	if (d->mode != HR_MODE_TWOPASS || !d->frame_start) {
		if (!d->frame_start)
			ST->skip_first++;
		goto right_blank;
	}

	tok = PER[7];			/* the game's token, put back at the end */
	if (!wait_gpu(d, tok == HR_TOKEN_A ? HR_TOKEN_B : HR_TOKEN_A, 1, d->frame_start)) {
		ST->skip_timeout1++;
		goto right_blank;
	}
	end = fifo_wp();

	dist = ring_dist(&r, d->frame_start, end);
	ST->last_frame = dist;
	if (dist > ST->max_frame)
		ST->max_frame = dist;
	if (dist == 0 || dist + 0x8000 > r.size) {
		ST->skip_big++;
		goto right_blank;
	}

	/* The frame start must still hold our prologue (FIFO not wrapped over it) */
	if (d->prologue_main) {
		at = find_scissor(&r, d->prologue_pos, 256);
		if (at == NOTFOUND) {
			ST->skip_overwritten++;
			goto right_blank;
		}
		found[nfound++] = at;
	} else if (find_bp(&r, d->prologue_pos, 288, BP_SCOFF_T0) == NOTFOUND) {
		ST->skip_overwritten++;
		goto right_blank;
	}

	/* The left half copy's destination address, to turn it into the right half */
	at = find_bp(&r, posL, 128, 0x4B000000 | (dst >> 5));
	if (at == NOTFOUND) {
		ST->skip_nocopy++;
		goto right_blank;
	}

	for (i = 0; i < d->nsc; i++) {
		u32 q = find_scissor(&r, d->sc_pos[i], 64);
		if (q == NOTFOUND)
			ST->sc_lost++;
		else
			found[nfound++] = q;
	}

	/* Patch the frame for the right half */
	patched = 0;
	if (!(d->test & (HR_TEST_SAME | HR_TEST_NOSC))) {
		for (i = 0; i < nfound; i++) {
			set_scissor_tile1(&r, found[i]);
			patched++;
		}
		ST2->vp_shifted += shift_viewports(&r, d->frame_start, dist);
	}
	if (!(d->test & (HR_TEST_SAME | HR_TEST_NOCOPY)))
		bp_set(&r, at, 0x4B000000 | ((dst + 1280) >> 5));
	if (d->test & HR_TEST_REWRITE) {	/* same values written back */
		bp_set(&r, at, 0x4B000000 | (dst >> 5));
		for (i = 0; i < nfound; i++) {
			bp_set(&r, found[i], BP_SCIS0_T0);
			bp_set(&r, ring_add(&r, found[i], 5), BP_SCIS1_T0);
			bp_set(&r, ring_add(&r, found[i], 10), BP_SCOFF_T0);
		}
	}
	ST->last_patched = patched;
	/*
		The game may poll its GXSetDrawSync token; ours replace it in the
		PE token register, so the last one the game sent is put back at
		the end (the value read at GXCopyDisp time can be older).
	*/
	ST->pe_nopped += nop_pe_interrupts(&r, d->frame_start, dist, &tok);
	ppc_sync();


	/*
		Pass 2: rewind the GPU to the start of the frame. The read/write
		distance register is moved back by the same amount as the read
		pointer, with interrupts off (nothing else may write the FIFO in
		between). Setting it from the write pointer instead left it 32
		bytes short now and then; once the GPU caught up it went negative,
		GX's overflow handler suspended the game's thread and nothing ever
		resumed it.
	*/
	msr = irq_off();
	en = CPR[1];
	CPR[1] = en & ~0x0D;		/* stop reading, no watermark interrupts */
	ppc_sync();
	wp = cp_get32(28) & 0x3FFFFFFF;	/* where the GPU stopped reading */
	cp_set32(28, d->frame_start);	/* read pointer */
	cp_set32(24, cp_get32(24) + ring_dist(&r, d->frame_start, wp));
	ppc_sync();
	CPR[1] = (en & ~0x0C) | 1;	/* read again */
	irq_restore(msr);
	if (wait_gpu(d, tok == 0x5A53 ? 0x5A54 : 0x5A53, 2, d->frame_start)) {
		u32 t0 = mftb();
		ST->replays++;
		/* until the GPU has read everything (our trailing NOPs too) */
		while (!(CPR[0] & 4) && mftb() - t0 < WAIT_TICKS / 100) ;
		msr = irq_off();
		if ((CPR[0] & 4) && cp_get32(24) != 0) {
			/* read idle but distance not 0: put it right */
			CPR[1] = en & ~0x0D;
			ppc_sync();
			cp_set32(24, 0);
			ST2->dist_fixed++;
		}
		CPR[2] = 3;		/* clear watermark status */
		CPR[1] = en;
		irq_restore(msr);
	} else {
		ST->timeout2++;
		ST2->hung_frames |= 1u << (ST->copies & 31);
		recover_gpu(en);
		if (++d->fails >= HR_MAX_FAILS && !(d->test & HR_TEST_KEEPON))
			d->mode = HR_MODE_LEFT;
	}
	bp_write(0x47000000 | tok);	/* the game's token value again */
	frame_prologue(d, &r);
	return;

right_blank:
	/* Right half from the cleared EFB, so it isn't left stale */
	orig((u8 *)dest + 1280, clear);
	if (d->mode == HR_MODE_TWOPASS && d->frame_start && tok != 0xFFFFFFFF)
		bp_write(0x47000000 | tok);
	frame_prologue(d, &r);
}

/*
	GXCallDisplayList: the FIFO only holds a call (address + size), and the
	game may rebuild that list (text, 2D, per object lists in a frame heap)
	before the replay, which then runs whatever is there now and can hang
	the GPU. Lists up to HR_DL_INLINE_MAX are copied into the FIFO instead,
	from memory as the GPU would read it, so the replay sees the same
	commands. The original function still runs first, with a list of NOPs,
	so GX flushes its pending state exactly as before.
*/
void hr_calldl(const void *list, u32 nbytes)
{
	struct hr_data *d = HR;
	calldl_fn orig = (calldl_fn)d->tramp_dl;
	u32 p = (u32)list & 0x3FFFFFFF;
	volatile u32 *src;
	u32 i, nop;

	if (d->mode != HR_MODE_TWOPASS || nbytes == 0 || nbytes > HR_DL_INLINE_MAX ||
	    (nbytes & 31) || (p & 31) || p + nbytes > 0x01800000) {
		ST2->dl_called++;
		if (nbytes > ST2->dl_big)
			ST2->dl_big = nbytes;
		orig(list, nbytes);
		return;
	}
	nop = (((u32)d->nop_dl + 31) & ~31) & 0x3FFFFFFF;
	orig((const void *)(0x80000000 | nop), 32);
	src = (volatile u32 *)(0xC0000000 | p);
	for (i = 0; i < nbytes / 4; i++)
		WGP32 = src[i];
	ST2->dl_inlined++;
	ST2->dl_bytes += nbytes;
	if (nbytes > ST2->dl_max)
		ST2->dl_max = nbytes;
}
