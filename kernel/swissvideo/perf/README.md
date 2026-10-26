# Frame-time profiler (Nintendont addition)

Shows how long each frame takes, and how that time splits between the CPU and
the GPU, on screen and in a log. It is part of the Swiss video patches, so it
runs whenever those are active (the Swiss video page), and is off when the
1280x240p experiment is on (both hook `GXCopyDisp`) or when cheats or the
debugger are enabled (they use the same low memory).

## On-screen overlay (top left, updated every 30 frames)

    FRAME 16.7 MAX 18.2     average and longest time between frames, ms
    MAIN  10.1 OTH  1.2     CPU: main thread / other threads, ms per frame
    IDLE   5.4 GPU 9.8      CPU idle, ms per frame / GPU busy, ms per frame
    FIFO  12K  SLOW 0       GPU command backlog at submit / frames over 20 ms

- **main** is the thread that calls `GXCopyDisp`, normally the game's main loop.
- **idle** is time with no game thread running (waiting for vsync, the GPU, or
  the disc).
- **gpu** is the share of the frame the GPU's command processor was busy.
- **fifo** is how many KB of commands the GPU still had to process when the
  game finished submitting the frame. Near 0 means the GPU keeps up; large
  values mean the CPU is ahead of the GPU.

Reading it: if MAIN is close to FRAME and IDLE is small, the game is
CPU-bound. If GPU is close to FRAME and FIFO is large, it's GPU-bound. If
both have room and FRAME is 16.7 (or 33.3), the game is waiting for vsync.

## Log

`/ninperf_NN.log` (first free number, so several runs can be kept) and
`/ninperf.log` (latest) on the boot device, one line every ~5 seconds with
the same numbers plus the sample count and the overlay's own cost.

## Settings (experiments build, `/nin960i_vi.txt`)

    perf=0      profiler off
    perfosd=0   log only, no overlay

## How it works

- A hook on `GXCopyDisp` marks frames and reads the timebase and the GPU FIFO
  distance.
- A performance-monitor interrupt (PMC1 counting CPU cycles, about 10,000 times a
  second) records whether a thread is running (`__OSCurrentThread` at
  0x800000E4, 0 when the OS is idle), whether it's the main thread, and
  whether the GPU is busy (CP status: not both "idle for reading" and "idle
  for commands"). The handler is installed at 0x80000F00 at the first frame,
  after the game's OS has set up its exception vectors.
- A hook on `__VIRetraceHandler` draws the overlay into the XFB the VI is
  about to show, during the vertical blank.

The interrupt can't sample while interrupts are disabled, so time spent in
interrupt handlers is not counted. GPU busy comes from the command processor,
so a short tail of pixel work after the last command isn't counted either.
