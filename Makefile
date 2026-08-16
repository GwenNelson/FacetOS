#
# FacetOS
#

ROOT := $(CURDIR)

VENV := $(ROOT)/.venv

SEL4_SRC     := $(ROOT)/external/seL4
SEL4_RUNTIME := $(ROOT)/external/sel4runtime

SDK_BUILD  := $(ROOT)/build/sdk
SDK        := $(ROOT)/sdk
SEL4_SDK   := $(SDK)/sel4

FACET_BUILD := $(ROOT)/build/facetos
FACET_INIT  := $(FACET_BUILD)/init

SEL4_CONFIG := $(ROOT)/config/FacetOS-seL4.cmake

SEL4_ENV := PATH="$(VENV)/bin:$(PATH)"

CC := gcc


#
# FacetOS compiler settings
#

FACET_CFLAGS := \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-mno-red-zone \
	-I$(SEL4_SDK)/libsel4/include \
	-I$(SDK)/sel4runtime/include

FACET_LDFLAGS := \
	-nostdlib \
	-static \
	-no-pie \
	-Wl,-u,_start \
	-Wl,-e,_start

FACET_OBJS := \
	$(FACET_BUILD)/init.o \
	$(FACET_BUILD)/sel4_bootinfo.o

SEL4RUNTIME_LIB := $(SDK)/sel4runtime/lib/libsel4runtime.a


.PHONY: all venv patches sdk facetos run clean sdk-clean image run


all: facetos


#
# Python environment used by the upstream seL4 build.
#

venv:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --upgrade pip setuptools
	$(VENV)/bin/pip install sel4-deps


#
# Patch nonsense
#

SEL4RUNTIME_PATCH_STAMP := $(SEL4_RUNTIME)/.facetos-patched

$(SEL4RUNTIME_PATCH_STAMP):
	git -C $(SEL4_RUNTIME) apply \
		$(ROOT)/patches/sel4runtime-gcc16.patch
	touch $@

patches: $(SEL4RUNTIME_PATCH_STAMP)


#
# Build the seL4 + sel4runtime SDK.
#
# This is deliberately separate from normal FacetOS builds.
#

sdk: patches
	mkdir -p $(SDK_BUILD)
	mkdir -p $(SDK)

	$(SEL4_ENV) cmake \
		-G Ninja \
		-C $(SEL4_CONFIG) \
		-DCMAKE_TOOLCHAIN_FILE=$(SEL4_SRC)/gcc.cmake \
		-DCMAKE_INSTALL_PREFIX=$(SDK) \
		-S $(ROOT)/cmake/sdk \
		-B $(SDK_BUILD)

	$(SEL4_ENV) ninja -C $(SDK_BUILD) kernel.elf sel4runtime

	$(SEL4_ENV) cmake --install $(SDK_BUILD)


#
# FacetOS
#

$(FACET_BUILD):
	mkdir -p $(FACET_BUILD)


#
# FacetOS root task.
#

$(FACET_BUILD)/init.o: src/init.c | $(FACET_BUILD)
	$(CC) $(FACET_CFLAGS) \
		-c $< \
		-o $@


#
# libsel4's IPC buffer storage.
#
# Most of libsel4 is header/generated syscall machinery, but
# sel4_bootinfo.c provides __sel4_ipc_buffer, which sel4runtime
# expects to exist.
#

$(FACET_BUILD)/sel4_bootinfo.o: $(SEL4_SDK)/libsel4/src/sel4_bootinfo.c | $(FACET_BUILD)
	$(CC) $(FACET_CFLAGS) \
		-c $< \
		-o $@


#
# Link the FacetOS root task.
#
# sel4runtime contains its own CRT/startup objects. Since they live
# inside a static archive, explicitly requiring _start causes the
# linker to extract the startup path from libsel4runtime.a.
#

$(FACET_INIT): $(FACET_OBJS) $(SEL4RUNTIME_LIB)
	$(CC) $(FACET_LDFLAGS) \
		-o $@ \
		$(FACET_OBJS) \
		$(SEL4RUNTIME_LIB)

	@echo
	@echo "FacetOS init linked successfully."
	@echo


facetos: $(FACET_INIT)


#
# QEMU & GRUB etc
#
#

ISO_ROOT := $(ROOT)/build/iso
FACETOS_ISO := $(ROOT)/build/facetos.iso

image: facetos
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub

	cp $(SDK_BUILD)/kernel/kernel.elf \
		$(ISO_ROOT)/boot/kernel.elf

	cp $(FACET_INIT) \
		$(ISO_ROOT)/boot/init

	cp $(ROOT)/boot/grub.cfg \
		$(ISO_ROOT)/boot/grub/grub.cfg

	grub-mkrescue \
		-o $(FACETOS_ISO) \
		$(ISO_ROOT)


run: image
	qemu-system-x86_64 \
		-enable-kvm \
		-cpu host \
		-m 512M \
		-cdrom $(FACETOS_ISO) \
		-serial stdio 


#
# Cleaning
#

clean:
	rm -rf $(FACET_BUILD)
	rm -rf $(ISO_ROOT)
	rm -rf $(FACETOS_ISO)


sdk-clean:
	rm -rf $(SDK_BUILD)
	rm -rf $(SDK)
	rm -rf $(SEL4_RUNTIME)/.facetos-patched
	git -C $(SEL4_RUNTIME) reset --hard
