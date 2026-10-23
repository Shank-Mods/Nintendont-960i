/*
	Swiss video settings (Nintendont 960i fork).

	These settings drive the video patches ported from Swiss
	(https://github.com/emukidid/swiss-gc, kernel/SwissVideo.c).
	They are kept out of NIN_CFG so nincfg.bin stays compatible with
	official Nintendont:

	* SWISS_CFG is what the user sets in the "Swiss Video" settings page.
	  The loader stores it in /nincfg_swiss.bin on the boot device.
	* SWISS_BOOT is what the loader works out from SWISS_CFG for the game
	  being started (Swiss' config_load_current()). The loader places it in
	  MEM2 next to NIN_CFG right before the kernel starts; the kernel only
	  reads SWISS_BOOT.

	Values use Swiss' own numbering so they can be passed to Patch_Video()
	unchanged (see gameVModeStr, aveCompatStr, ... in Swiss gui/settings.c).
*/
#ifndef __SWISS_VIDEO_CONFIG_H__
#define __SWISS_VIDEO_CONFIG_H__

#define SWISS_CFG_MAGIC		0x53575643	/* 'SWVC' */
#define SWISS_CFG_VERSION	1
#define SWISS_BOOT_MAGIC	0x53575642	/* 'SWVB' */

/* MEM2 locations, after NIN_CFG (0x93004000, 0x224 bytes) */
#define SWISS_CFG_ADDR_PPC	0x93004800
#define SWISS_BOOT_ADDR_PPC	0x93004840
#define SWISS_BOOT_ADDR_ARM	0x13004840

/* Swiss "AVE Compatibility" (enum aveCompat in Swiss) */
enum swissAveCompat
{
	SWISS_AVE_N_DOL = 0,
	SWISS_AVE_P_DOL,
	SWISS_CMPV_DOL,
	SWISS_GCDIGITAL,
	SWISS_GCVIDEO,
	SWISS_AVE_RVL,
	SWISS_AVE_COMPAT_MAX
};

/*
	Swiss video modes (gameVMode):
	0 Auto, 1 480i, 2 480sf, 3 240p, 4 960i, 5 480p, 6 1080i60, 7 540p60,
	8 576i, 9 576sf, 10 288p, 11 1152i, 12 576p, 13 1080i50, 14 540p50
	NTSC games can use 0-7, PAL games 0-14.
*/
#define SWISS_VMODE_NTSC_MAX	8
#define SWISS_VMODE_PAL_MAX	15

/* What the user sets. Stored as-is in /nincfg_swiss.bin. */
typedef struct SWISS_CFG
{
	unsigned int	Magic;		// SWISS_CFG_MAGIC
	unsigned int	Version;	// SWISS_CFG_VERSION
	unsigned char	Enabled;	// Master switch: use the Swiss video patches
	unsigned char	AveCompat;	// enum swissAveCompat
	unsigned char	ForceDTVStatus;	// 0 No, 1 Yes, 2 Region Switch
	unsigned char	RT4KOptim;	// RetroTINK-4K HDMI input (GCDigital / GCVideo)
	unsigned char	ForceVideoActive;	// GCVideo-DVI 3.0 workaround
	unsigned char	VideoModeNtsc;	// gameVMode for NTSC games (0-7)
	unsigned char	VideoModePal;	// gameVMode for PAL games (0-14)
	unsigned char	ForceHScale;	// 0 Auto, 1:1, 11:10, 9:8, 640, 656, 672, 704, 720px
	signed short	ForceVOffset;	// lines
	unsigned char	ForceVFilter;	// 0 Auto, 1-3 = filter 0-2
	unsigned char	ForceVJitter;	// Force Field Rendering: 0 Auto, 1 On, 2 Off, 3 TAA
	unsigned char	FixPixelCenter;	// 0 No, 1 1/24, 2 1/12
	unsigned char	DisableDithering;	// Disable Alpha Dithering
	unsigned char	ForceAnisotropy;	// Force Anisotropic Filter
	unsigned char	ForceWidescreen;	// 0 No, 1 3D, 2 2D+3D
	unsigned char	InvertFieldOrder;	// Nintendont addition: flip the field parity
	unsigned char	HiRes240p;	// Nintendont experiment: 0 Off, 1 Left half, 2 Two-pass
	unsigned char	Reserved[6];
} SWISS_CFG;

/* What the kernel uses for the game being booted. */
typedef struct SWISS_BOOT
{
	unsigned int	Magic;		// SWISS_BOOT_MAGIC; anything else = patches off
	signed char	gameVMode;	// resolved: region, PAL-only games, DTV checked
	unsigned char	aveCompat;
	unsigned char	forceDTVStatus;
	unsigned char	rt4kOptim;
	unsigned char	forceVideoActive;
	unsigned char	forceHScale;
	signed short	forceVOffset;
	unsigned char	forceVFilter;
	unsigned char	forceVJitter;
	unsigned char	fixPixelCenter;
	unsigned char	disableDithering;
	unsigned char	forceAnisotropy;
	unsigned char	forceWidescreen;
	unsigned char	sramVideo;	// 0 NTSC, 1 PAL, 2 MPAL
	unsigned char	sram60Hz;	// only meaningful when gameVMode > 0
	unsigned char	sramProgressive;	// only meaningful when gameVMode > 0
	unsigned char	invertFieldOrder;	// flip Swiss' forced field rendering parity
	unsigned char	hiRes240p;	// 1280x240p experiment (HR_MODE_*), NTSC 480p path only
	unsigned char	Reserved[9];
} SWISS_BOOT;

#endif
