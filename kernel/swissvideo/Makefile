# Swiss video patch stubs (PowerPC), converted to C headers for the kernel.

PREFIX	= $(DEVKITPPC)/bin/powerpc-eabi-
CC	= $(PREFIX)gcc
OBJCOPY	= $(PREFIX)objcopy

ifeq ($(OS),Windows_NT)
	BIN2H =	../bin2h/bin2h.exe
else
	BIN2H =	../bin2h/bin2h
endif

SFILES	:= $(wildcard stubs/*.S)
OFILES	:= $(SFILES:.S=.o)
BINFILES:= $(SFILES:.S=.bin)
HFILES	:= $(SFILES:.S=.h)

ASFLAGS	:= -mregnames -D_LANGUAGE_ASSEMBLY -I.

.PHONY: all clean
.PRECIOUS: %.o %.bin

all: $(HFILES) hires/hires_payload.h

# 1280x240p two-pass payload (position independent PowerPC code, no data).
# Linked at two addresses: the blobs must be identical.
HR_CFLAGS := -O2 -mcpu=750 -meabi -mhard-float -mno-sdata -fno-pic -fno-PIC \
	-ffreestanding -fno-builtin -fno-jump-tables -fno-common \
	-fno-asynchronous-unwind-tables -nostdlib -Wall -Ihires

hires/hires_entry.o: hires/hires_entry.S hires/hires_shared.h
	@echo " ASSEMBLE    $<"
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -Ihires -c $< -o $@

hires/hires.o: hires/hires.c hires/hires_shared.h
	@echo " COMPILE     $<"
	@$(CC) $(HR_CFLAGS) -c $< -o $@

hires/hires_payload.bin: hires/hires_entry.o hires/hires.o hires/hires.ld
	@echo " LINK        $@"
	@$(CC) -nostdlib -nostartfiles -Wl,--defsym=HR_LINK_ADDR=0 -T hires/hires.ld \
		hires/hires_entry.o hires/hires.o -o hires/hires_a.elf
	@$(CC) -nostdlib -nostartfiles -Wl,--defsym=HR_LINK_ADDR=0x10000 -T hires/hires.ld \
		hires/hires_entry.o hires/hires.o -o hires/hires_b.elf
	@$(OBJCOPY) -O binary -j .text hires/hires_a.elf hires/hires_a.bin
	@$(OBJCOPY) -O binary -j .text hires/hires_b.elf hires/hires_b.bin
	@cmp -s hires/hires_a.bin hires/hires_b.bin || \
		{ echo "hires payload is not position independent"; exit 1; }
	@! $(PREFIX)objdump -h hires/hires_a.elf | grep -E " \.(s?data|s?bss|rodata|sdata2|got)" || \
		{ echo "hires payload must not have data sections"; exit 1; }
	@cp hires/hires_a.bin $@

hires/hires_payload.h: hires/hires_payload.bin
	@echo " BIN2H       $<"
	@cd hires && ../$(BIN2H) hires_payload.bin

stubs/%.o: stubs/%.S reservedarea.h
	@echo " ASSEMBLE    $<"
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

stubs/%.bin: stubs/%.o
	@$(OBJCOPY) -O binary -j .text $< $@

stubs/%.h: stubs/%.bin
	@echo " BIN2H       $<"
	@cd stubs && ../$(BIN2H) $(notdir $<)

clean:
	-$(RM) -f stubs/*.o stubs/*.bin stubs/*.h
	-$(RM) -f hires/*.o hires/*.elf hires/*.bin hires/hires_payload.h
