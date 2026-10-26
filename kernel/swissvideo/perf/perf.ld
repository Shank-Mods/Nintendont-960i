/* Frame-time profiler payload: one flat, position independent code blob. */
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)
SECTIONS
{
	. = PF_LINK_ADDR;
	.text : {
		*(.text.entry)
		*(.text .text.*)
	}
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
	ASSERT(SIZEOF(.text) > 0, "empty payload")
}
