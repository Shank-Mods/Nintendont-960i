/*
	Frame-time profiler: layout shared by the kernel (SwissVideo.c) and the
	PowerPC payload (perf.c / perf_entry.S). Nintendont addition.

	The payload hooks GXCopyDisp (one call per frame) to time frames and
	draw an overlay into the XFB. A performance-monitor interrupt (PMC1
	overflow, ~10 kHz) samples what the CPU and GPU are doing:
	  - current OSThread (0x800000E4): 0 = CPU idle, the thread that calls
	    GXCopyDisp = main thread, anything else = other threads
	  - CP status: GPU busy unless both "idle for reading" and "idle for
	    commands" are set
*/
#ifndef __PERF_SHARED_H__
#define __PERF_SHARED_H__

/* Payload code, MEM2 (the PPC executes it there, like the 1280x240p payload) */
#define PF_PAYLOAD_PPC		0x93178000
#define PF_PAYLOAD_ARM		0x13178000
#define PF_PAYLOAD_MAX		0x6000

/* Shared data, MEM2: uncached for the PPC, ARM view */
#define PF_DATA_PPC		0xD3180000
#define PF_DATA_ARM		0x13180000
#define PF_DATA_HI		0xD318

/*
	Hook trampolines + absolute jumps, low MEM1 (free without cheats/debugger):
	+0x00 GXCopyDisp trampoline (first insn, b back), +0x08 jump to payload +0x00
	+0x18 __VIRetraceHandler trampoline, +0x20 jump to payload +0x0C
*/
#define PF_BLOCK		0x80001000
#define PF_TRAMP_COPYDISP	0x00
#define PF_JMP_COPYDISP		0x08
#define PF_TRAMP_RETRACE	0x18
#define PF_JMP_RETRACE		0x20
#define PF_ENTRY_COPYDISP	0x00
#define PF_ENTRY_RETRACE	0x0C
/* Interrupt handler register save area, physical (real mode) */
#define PF_SCRATCH		0x1100

#define PF_MAGIC		0x50455246	/* 'PERF' */
/* MMCR0: ENINT | PMC1INTCONTROL | PMC1SELECT = 1 (processor cycles) */
#define PF_MMCR0_VALUE		0x04008040
#define PF_SAMPLE_HZ		10000
#define PF_WINDOW		30		/* frames per published average */

/* Byte offsets used by the interrupt handler (asm) */
#define PF_OFF_MAGIC		0x00
#define PF_OFF_S_TOTAL		0x04
#define PF_OFF_S_IDLE		0x08
#define PF_OFF_S_MAIN		0x0C
#define PF_OFF_S_GPU		0x10
#define PF_OFF_MAIN_THREAD	0x14
#define PF_OFF_RELOAD		0x18
#define PF_OFF_MMCR0		0x1C

/* Payload header: +0 branch to the GXCopyDisp hook, +4 handler offset, +8 handler size,
   +0x0C branch to the __VIRetraceHandler hook */
#define PF_HDR_HANDLER_OFF	4
#define PF_HDR_HANDLER_LEN	8
#define PF_HANDLER_MAX		0x100	/* 0x80000F00 - 0x80000FFF */

#ifndef _LANGUAGE_ASSEMBLY

struct pf_data {
	/* 0x00: written by the interrupt handler (offsets above) */
	unsigned int magic;
	unsigned int s_total;		/* samples */
	unsigned int s_idle;		/* CPU idle (no current thread) */
	unsigned int s_main;		/* main (drawing) thread running */
	unsigned int s_gpu;		/* GPU busy */
	unsigned int main_thread;	/* OSThread that calls GXCopyDisp */
	unsigned int reload;		/* PMC1 reload value */
	unsigned int mmcr0;		/* MMCR0 value */

	/* 0x20: payload state */
	unsigned int enabled;		/* kernel: 1 = run */
	unsigned int osd;		/* kernel: 1 = draw the overlay */
	unsigned int installed;		/* interrupt handler installed */
	unsigned int copies;		/* GXCopyDisp calls */
	unsigned int last_tb;
	unsigned int osd_xfb;		/* XFB the overlay was last drawn into */
	unsigned int l_total, l_idle, l_main, l_gpu;
	unsigned int tb_per_10us;	/* timebase ticks per 10 us */
	unsigned int cpu_hz, bus_hz;

	/* window accumulators (10 us units, bytes) */
	unsigned int w_n, w_frame, w_max_frame, w_main, w_other, w_idle, w_gpu;
	unsigned int w_fifo, w_max_fifo, w_slow, w_samples, w_ovl;

	/* published averages of the last full window (10 us units) */
	unsigned int seq;
	unsigned int r_n, r_frame, r_max_frame, r_main, r_other, r_idle, r_gpu;
	unsigned int r_fifo, r_max_fifo, r_slow, r_samples, r_ovl;

	unsigned int retraces;		/* __VIRetraceHandler calls */
	unsigned int reserved[7];

	/* 8x8 font for ASCII 0x20..0x5F, written by the kernel */
	unsigned char font[64][8];
};

#endif
#endif
