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

all: $(HFILES)

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
