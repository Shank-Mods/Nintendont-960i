# Host test harness for the Swiss video patcher, see swissvideo_test.c.
# Needs a big endian 32-bit PowerPC Linux cross compiler and qemu-ppc.

CC	?= powerpc-linux-gnu-gcc
CFLAGS	:= -O1 -g -Wall -Wno-format -std=gnu99 -static -DSWISSVIDEO_HOST_TEST -iquote ../.. -iquote .

swissvideo_test: swissvideo_test.c ../../SwissVideo.c host_env.h ../Patch_Video.inc ../Patch_Extras.inc ../stubs.h
	$(CC) $(CFLAGS) -o $@ swissvideo_test.c ../../SwissVideo.c

clean:
	rm -f swissvideo_test
