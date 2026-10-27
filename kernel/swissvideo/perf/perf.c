/*
	Frame-time profiler payload (PowerPC, position independent, no data).
	Nintendont addition. See perf_shared.h.

	Called from the GXCopyDisp hook once per frame:
	  - frame interval from the timebase
	  - CPU main / other / idle and GPU busy shares from the interrupt
	    samples since the last frame, turned into ms of this frame
	  - GPU FIFO backlog (CP read/write distance) when the frame is submitted
	  - every PF_WINDOW frames the averages are published for the overlay
	    and the kernel's log

	Called after every __VIRetraceHandler (vertical blank, interrupts off):
	  - the overlay is drawn into the XFB the VI scans out next. The game
	    only points the VI at a buffer once its copy is done, and the top
	    lines aren't scanned until the blanking interval is over.
*/
#include "perf_shared.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define D		((volatile struct pf_data *)PF_DATA_PPC)
#define CP16(off)	(*(volatile u16 *)(0xCC000000 + (off)))
#define VI16(off)	(*(volatile u16 *)(0xCC002000 + (off)))

static inline u32 mftb(void)
{
	u32 t;
	__asm__ volatile ("mftb %0" : "=r" (t));
	return t;
}

static inline void mtpmc1(u32 v) { __asm__ volatile ("mtspr 953, %0" : : "r" (v)); }
static inline void mtmmcr0(u32 v) { __asm__ volatile ("mtspr 952, %0" : : "r" (v)); }

static inline u32 fifo_distance(void)
{
	u32 hi, lo, hi2;
	do {
		hi = CP16(0x32);
		lo = CP16(0x30);
		hi2 = CP16(0x32);
	} while (hi != hi2);
	return (hi << 16) | lo;
}

/* Copy the interrupt handler to vector 0x0F00 and start the counter. */
static void pf_install(volatile struct pf_data *d)
{
	const u32 *hdr = (const u32 *)PF_PAYLOAD_PPC;
	const u32 *src = (const u32 *)(PF_PAYLOAD_PPC + hdr[PF_HDR_HANDLER_OFF / 4]);
	u32 len = hdr[PF_HDR_HANDLER_LEN / 4];
	volatile u32 *dst = (volatile u32 *)0x80000F00;
	u32 i, period;

	if (len == 0 || len > PF_HANDLER_MAX)
		return;
	for (i = 0; i < len / 4; i++)
		dst[i] = src[i];
	for (i = 0; i < len; i += 32)
		__asm__ volatile ("dcbst 0, %0" : : "r" (0x80000F00 + i));
	__asm__ volatile ("sync");
	for (i = 0; i < len; i += 32)
		__asm__ volatile ("icbi 0, %0" : : "r" (0x80000F00 + i));
	__asm__ volatile ("sync; isync");

	d->cpu_hz = *(volatile u32 *)0x800000FC;
	d->bus_hz = *(volatile u32 *)0x800000F8;
	d->tb_per_10us = d->bus_hz / 4 / 100000;
	if (d->tb_per_10us == 0)
		d->tb_per_10us = 405;
	period = d->cpu_hz / PF_SAMPLE_HZ;
	if (period < 1000)
		period = 48600;
	d->reload = 0x80000000 - period;
	d->mmcr0 = PF_MMCR0_VALUE;
	d->installed = 1;
	mtpmc1(d->reload);
	mtmmcr0(d->mmcr0);
}

/* ---------------------------------------------------------------- overlay */

/* Appends ms with one decimal from a 10 us value ("16.7") */
static int put_ms(char *s, int n, u32 v10us)
{
	u32 tenths = v10us / 10;
	u32 ip = tenths / 10, fp = tenths % 10;
	char tmp[8];
	int k = 0;
	if (ip > 999)
		ip = 999;
	do {
		tmp[k++] = '0' + ip % 10;
		ip /= 10;
	} while (ip);
	while (k)
		s[n++] = tmp[--k];
	s[n++] = '.';
	s[n++] = '0' + fp;
	return n;
}

static int put_u(char *s, int n, u32 v)
{
	char tmp[10];
	int k = 0;
	do {
		tmp[k++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (k)
		s[n++] = tmp[--k];
	return n;
}

static int pad(char *s, int n, int to)
{
	while (n < to)
		s[n++] = ' ';
	return n;
}

#define OSD_X		32	/* pixels, multiple of 16 */
#define OSD_Y		24	/* XFB lines */
#define OSD_CHARS	20
#define OSD_LINEH	18	/* 16 + 2 lines of background between rows */

/*
	One text row into the XFB (YUV 4:2:2, 2 bytes per pixel). Each glyph
	pixel is 2 pixels wide (one Y0 U Y1 V word) and 2 lines tall; white on
	black so the box hides what is under it.
*/
static void draw_row(volatile struct pf_data *d, u32 xfb, u32 stride, u32 y, const char *s)
{
	u32 line, c, k;
	for (line = 0; line < OSD_LINEH; line++) {
		u32 *p = (u32 *)(xfb + (y + line) * stride + OSD_X * 2);
		u32 grow = line / 2;
		for (c = 0; c < OSD_CHARS; c++) {
			u32 ch = (u8)s[c];
			u32 bits = 0;
			if (grow < 8 && ch >= 0x20 && ch < 0x60)
				bits = d->font[ch - 0x20][grow];
			for (k = 0; k < 8; k++) {
				u32 y8 = (bits & (0x80 >> k)) ? 235 : 16;
				*p++ = (y8 << 24) | (128 << 16) | (y8 << 8) | 128;
			}
		}
		/* flush this line's span (32-byte lines) */
		{
			u32 a = (xfb + (y + line) * stride + OSD_X * 2) & ~31;
			u32 end = xfb + (y + line) * stride + OSD_X * 2 + OSD_CHARS * 32;
			for (; a < end; a += 32)
				__asm__ volatile ("dcbf 0, %0" : : "r" (a));
		}
	}
}

static void draw_osd(volatile struct pf_data *d, u32 xfb_virt)
{
	u32 phys = xfb_virt & 0x0FFFFFFF;
	/*
		PICCONF: bits 8-14 = 16-pixel words read per line. (Bits 0-7 are the
		stride per field line, twice that in interlaced modes.) Drawing in
		frame lines covers both fields.
	*/
	u32 stride = ((VI16(0x48) >> 8) & 0x7F) * 32;
	u32 xfb;
	char s[OSD_CHARS + 4];
	int n;

	if (phys >= 0x01800000 || stride < 640 || stride > 4096)
		return;
	xfb = 0x80000000 | phys;	/* cached, flushed per line */

	/* FRAME 16.7 MAX 18.2 */
	n = 0;
	s[n++] = 'F'; s[n++] = 'R'; s[n++] = 'A'; s[n++] = 'M'; s[n++] = 'E'; s[n++] = ' ';
	n = put_ms(s, n, d->r_frame);
	s[n++] = ' '; s[n++] = 'M'; s[n++] = 'A'; s[n++] = 'X'; s[n++] = ' ';
	n = put_ms(s, n, d->r_max_frame);
	pad(s, n, OSD_CHARS);
	draw_row(d, xfb, stride, OSD_Y, s);

	/* MAIN 10.1 OTH 1.2 */
	n = 0;
	s[n++] = 'M'; s[n++] = 'A'; s[n++] = 'I'; s[n++] = 'N'; s[n++] = ' ';
	n = put_ms(s, n, d->r_main);
	s[n++] = ' '; s[n++] = 'O'; s[n++] = 'T'; s[n++] = 'H'; s[n++] = ' ';
	n = put_ms(s, n, d->r_other);
	pad(s, n, OSD_CHARS);
	draw_row(d, xfb, stride, OSD_Y + OSD_LINEH, s);

	/* IDLE 5.4 GPU 9.8 */
	n = 0;
	s[n++] = 'I'; s[n++] = 'D'; s[n++] = 'L'; s[n++] = 'E'; s[n++] = ' ';
	n = put_ms(s, n, d->r_idle);
	s[n++] = ' '; s[n++] = 'G'; s[n++] = 'P'; s[n++] = 'U'; s[n++] = ' ';
	n = put_ms(s, n, d->r_gpu);
	pad(s, n, OSD_CHARS);
	draw_row(d, xfb, stride, OSD_Y + 2 * OSD_LINEH, s);

	/* FIFO 12K SLOW 0 */
	n = 0;
	s[n++] = 'F'; s[n++] = 'I'; s[n++] = 'F'; s[n++] = 'O'; s[n++] = ' ';
	n = put_u(s, n, (d->r_fifo + 1023) / 1024);
	s[n++] = 'K'; s[n++] = ' '; s[n++] = 'S'; s[n++] = 'L'; s[n++] = 'O'; s[n++] = 'W'; s[n++] = ' ';
	n = put_u(s, n, d->r_slow);
	pad(s, n, OSD_CHARS);
	draw_row(d, xfb, stride, OSD_Y + 3 * OSD_LINEH, s);
}

/* ---------------------------------------------------------------- per frame */

void pf_frame(u32 dest)
{
	volatile struct pf_data *d = D;
	u32 now = mftb();
	u32 tot, idle, mainc, gpu, fus, fifo;
	u32 m = 0, o = 0, i = 0, g = 0;

	if (d->magic != PF_MAGIC || !d->enabled)
		return;
	d->main_thread = *(volatile u32 *)0x800000E4;
	d->copies++;

	if (!d->installed) {
		pf_install(d);
		d->last_tb = now;
		d->l_total = d->s_total;
		d->l_idle = d->s_idle;
		d->l_main = d->s_main;
		d->l_gpu = d->s_gpu;
		return;
	}

	fus = (now - d->last_tb) / d->tb_per_10us;
	d->last_tb = now;

	tot = d->s_total - d->l_total;
	idle = d->s_idle - d->l_idle;
	mainc = d->s_main - d->l_main;
	gpu = d->s_gpu - d->l_gpu;
	d->l_total += tot;
	d->l_idle += idle;
	d->l_main += mainc;
	d->l_gpu += gpu;

	if (tot) {
		m = fus * mainc / tot;
		i = fus * idle / tot;
		g = fus * gpu / tot;
		o = (tot > mainc + idle) ? fus * (tot - mainc - idle) / tot : 0;
	}
	fifo = fifo_distance();

	d->w_n++;
	d->w_frame += fus;
	if (fus > d->w_max_frame)
		d->w_max_frame = fus;
	d->w_main += m;
	d->w_other += o;
	d->w_idle += i;
	d->w_gpu += g;
	d->w_fifo += fifo;
	if (fifo > d->w_max_fifo)
		d->w_max_fifo = fifo;
	if (fus > 2000)		/* > 20 ms: missed a 60 Hz field */
		d->w_slow++;
	d->w_samples += tot;

	if (d->w_n >= PF_WINDOW) {
		u32 n = d->w_n;
		d->r_n = n;
		d->r_frame = d->w_frame / n;
		d->r_max_frame = d->w_max_frame;
		d->r_main = d->w_main / n;
		d->r_other = d->w_other / n;
		d->r_idle = d->w_idle / n;
		d->r_gpu = d->w_gpu / n;
		d->r_fifo = d->w_fifo / n;
		d->r_max_fifo = d->w_max_fifo;
		d->r_slow = d->w_slow;
		d->r_samples = d->w_samples;
		d->r_ovl = d->w_ovl / n;
		d->seq++;
		d->w_n = d->w_frame = d->w_max_frame = d->w_main = d->w_other = 0;
		d->w_idle = d->w_gpu = d->w_fifo = d->w_max_fifo = d->w_slow = 0;
		d->w_samples = d->w_ovl = 0;
	}

	(void)dest;
}

/* ---------------------------------------------------------------- per field */

void pf_retrace(void)
{
	volatile struct pf_data *d = D;
	u32 tfbl, bfbl, b, xfb, t0;

	if (d->magic != PF_MAGIC)
		return;
	d->retraces++;

	/* BT.656: the game's VI code writes DCR / leaves the border off; put ours back */
	if (d->bt656) {
		u32 dcr = VI16(0x02);
		if ((dcr & 0x0300) != 0x0300) {
			VI16(0x02) = dcr | 0x0300;
			d->b656_fixes++;
		}
		if (VI16(0x72) != (u16)d->b656_r72 || VI16(0x74) != (u16)d->b656_r74) {
			VI16(0x72) = (u16)d->b656_r72;
			VI16(0x74) = (u16)d->b656_r74;
			d->b656_fixes++;
		}
	}

	if (!d->enabled || !d->osd || !d->seq || !d->installed)
		return;
	/* field bases (address >> 5 when bit 28 is set); the frame starts at the lower one */
	tfbl = ((u32)VI16(0x1C) << 16) | VI16(0x1E);
	xfb = tfbl & 0x00FFFFFF;
	if (tfbl & 0x10000000)
		xfb <<= 5;
	bfbl = ((u32)VI16(0x24) << 16) | VI16(0x26);
	b = bfbl & 0x00FFFFFF;
	if (bfbl & 0x10000000)
		b <<= 5;
	if (b && b < xfb)
		xfb = b;
	if (xfb == 0)
		return;
	t0 = mftb();
	draw_osd(d, xfb);
	d->osd_xfb = xfb;
	d->w_ovl += (mftb() - t0) / d->tb_per_10us;
}
