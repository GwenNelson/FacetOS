# Tiny standalone build for bootstub32.

CC ?= gcc
LD ?= ld

CFLAGS := \
	-m32 \
	-std=gnu11 \
	-ffreestanding \
	-fno-pic \
	-fno-pie \
	-fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-Wall \
	-Wextra \
	-Werror \
	-O2

ASFLAGS := \
	-m32 \
	-ffreestanding \
	-fno-pic \
	-fno-pie

all: bootstub32

bootstub32.o: bootstub32.S
	$(CC) $(ASFLAGS) -c $< -o $@

bootstub32_c.o: bootstub32.c
	$(CC) $(CFLAGS) -c $< -o $@

bootstub32: bootstub32.o bootstub32_c.o bootstub32.ld
	$(LD) -m elf_i386 -T bootstub32.ld \
		-o $@ bootstub32.o bootstub32_c.o

clean:
	rm -f bootstub32 bootstub32.o bootstub32_c.o

.PHONY: all clean
