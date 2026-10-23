/* 1280x240p payload: one flat, position independent code blob. */
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)
SECTIONS
{
	. = HR_LINK_ADDR;
	.text : {
		*(.text.entry)
		*(.text .text.*)
	}
	/* The payload must not have data of its own (no absolute addresses) */
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
	ASSERT(SIZEOF(.text) > 0, "empty payload")
}
