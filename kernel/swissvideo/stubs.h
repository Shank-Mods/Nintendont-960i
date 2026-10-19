/* Swiss video stubs, built from the stubs directory by kernel/swissvideo/Makefile */
#ifndef __SWISSVIDEO_STUBS_H__
#define __SWISSVIDEO_STUBS_H__

#include "stubs/GXAdjustForOverscanPatch.h"
#include "stubs/GXCopyDispHook.h"
#include "stubs/GXSetBlendModePatch1.h"
#include "stubs/GXSetBlendModePatch2.h"
#include "stubs/GXSetBlendModePatch3.h"
#include "stubs/GXSetCopyFilterPatch.h"
#include "stubs/GXSetDispCopyYScalePatch1.h"
#include "stubs/GXSetDispCopyYScalePatch2.h"
#include "stubs/GXSetDispCopyYScaleStub1.h"
#include "stubs/GXSetDispCopyYScaleStub2.h"
#include "stubs/GXSetViewportJitterPatch1.h"
#include "stubs/GXSetViewportJitterPatch2.h"
#include "stubs/GXSetViewportJitterPatch3.h"
#include "stubs/GXSetViewportJitterTAAPatch1.h"
#include "stubs/GXSetViewportJitterTAAPatch3.h"
#include "stubs/GXSetViewportPatch1.h"
#include "stubs/GXSetViewportPatch2.h"
#include "stubs/GXSetViewportPatch3.h"
#include "stubs/GXSetViewportTAAPatch3.h"
#include "stubs/VIConfigure1080i50.h"
#include "stubs/VIConfigure1080i60.h"
#include "stubs/VIConfigure1152i.h"
#include "stubs/VIConfigure240p.h"
#include "stubs/VIConfigure288p.h"
#include "stubs/VIConfigure480i.h"
#include "stubs/VIConfigure480p.h"
#include "stubs/VIConfigure540p50.h"
#include "stubs/VIConfigure540p60.h"
#include "stubs/VIConfigure576i.h"
#include "stubs/VIConfigure576p.h"
#include "stubs/VIConfigure960i.h"
#include "stubs/VIConfigureAutop.h"
#include "stubs/VIConfigureFieldMode.h"
#include "stubs/VIConfigureHook1.h"
#include "stubs/VIConfigureHook1GCVideo.h"
#include "stubs/VIConfigureHook1RT4K.h"
#include "stubs/VIConfigureHook2.h"
#include "stubs/VIConfigureNoYScale.h"
#include "stubs/VIConfigurePanHook.h"
#include "stubs/VIConfigurePanHookD.h"
#include "stubs/VIGetRetraceCountHook.h"
#include "stubs/VIRetraceHandlerHook.h"
#include "stubs/__GXSetViewportPatch.h"
#include "stubs/__GXSetViewportTAAPatch.h"
#include "stubs/getTimingPatch.h"

/* Swiss refers to in-place patches by their length in words */
#define GXAdjustForOverscanPatchLength (GXAdjustForOverscanPatch_size / 4)
#define GXSetBlendModePatch1Length (GXSetBlendModePatch1_size / 4)
#define GXSetBlendModePatch2Length (GXSetBlendModePatch2_size / 4)
#define GXSetBlendModePatch3Length (GXSetBlendModePatch3_size / 4)
#define GXSetCopyFilterPatchLength (GXSetCopyFilterPatch_size / 4)
#define GXSetDispCopyYScalePatch1Length (GXSetDispCopyYScalePatch1_size / 4)
#define GXSetDispCopyYScalePatch2Length (GXSetDispCopyYScalePatch2_size / 4)
#define GXSetDispCopyYScaleStub1Length (GXSetDispCopyYScaleStub1_size / 4)
#define GXSetDispCopyYScaleStub2Length (GXSetDispCopyYScaleStub2_size / 4)
#define GXSetViewportJitterPatch1Length (GXSetViewportJitterPatch1_size / 4)
#define GXSetViewportJitterPatch2Length (GXSetViewportJitterPatch2_size / 4)
#define GXSetViewportJitterPatch3Length (GXSetViewportJitterPatch3_size / 4)
#define GXSetViewportJitterTAAPatch1Length (GXSetViewportJitterTAAPatch1_size / 4)
#define GXSetViewportJitterTAAPatch3Length (GXSetViewportJitterTAAPatch3_size / 4)
#define GXSetViewportPatch1Length (GXSetViewportPatch1_size / 4)
#define GXSetViewportPatch2Length (GXSetViewportPatch2_size / 4)
#define GXSetViewportPatch3Length (GXSetViewportPatch3_size / 4)
#define GXSetViewportTAAPatch3Length (GXSetViewportTAAPatch3_size / 4)
#define __GXSetViewportPatchLength (__GXSetViewportPatch_size / 4)
#define __GXSetViewportTAAPatchLength (__GXSetViewportTAAPatch_size / 4)
#define getTimingPatchLength (getTimingPatch_size / 4)

#endif
