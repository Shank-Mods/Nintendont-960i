#include "reservedarea.h"

.globl VIGetRetraceCountHook
VIGetRetraceCountHook:
	lis			%r4, VAR_AREA
	lbz			%r4, VAR_CURRENT_FIELD (%r4)
	sub			%r3, %r3, %r4
	blr
