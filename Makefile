#
# FacetOS
#

ROOT := $(CURDIR)

VENV := $(ROOT)/.venv

SEL4_SRC     := $(ROOT)/external/seL4
SEL4_RUNTIME := $(ROOT)/external/sel4runtime

SDK_BUILD := $(ROOT)/build/sdk
SDK       := $(ROOT)/sdk
SEL4_SDK  := $(SDK)/sel4

FACET_BUILD          := $(ROOT)/build/facetos
FACET_DOMINIT0_BUILD := $(FACET_BUILD)/dominit0
FACET_DOMINIT0       := $(FACET_DOMINIT0_BUILD)/dominit0

BOOTSTUB32_DIR := $(ROOT)/bootstub32
BOOTSTUB32     := $(BOOTSTUB32_DIR)/bootstub32

SEL4_CONFIG := $(ROOT)/config/FacetOS-seL4.cmake

SEL4_ENV := PATH="$(VENV)/bin:$(PATH)"

CC := gcc


#
# FacetOS "kernel" (seL4 tasks) compiler settings
#

FACET_CFLAGS := \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-mno-red-zone \
	-MMD \
	-MP

ifeq ($(DEBUG),1)
	FACET_CFLAGS += -Og -g -DDEBUG
else
	FACET_CFLAGS += -O2
endif

SEL4_INCLUDES := \
	-I$(ROOT)/external/seL4/libsel4/include \
	-I$(ROOT)/external/seL4/libsel4/arch_include/x86 \
	-I$(ROOT)/external/seL4/libsel4/sel4_arch_include/x86_64 \
	-I$(ROOT)/external/seL4/libsel4/sel4_plat_include/pc99 \
	-I$(ROOT)/external/seL4/libsel4/mode_include/64 \
	-I$(ROOT)/build/sdk/libsel4/include \
	-I$(ROOT)/build/sdk/libsel4/arch_include/x86 \
	-I$(ROOT)/build/sdk/libsel4/sel4_arch_include/x86_64 \
	-I$(ROOT)/build/sdk/libsel4/autoconf \
	-I$(ROOT)/build/sdk/libsel4/gen_config \
	-I$(ROOT)/build/sdk/kernel/gen_config \
	-I$(ROOT)/external/sel4runtime/include \
	-I$(ROOT)/external/sel4runtime/include/mode/64 \
	-I$(ROOT)/external/sel4runtime/include/arch/x86 \
	-I$(ROOT)/external/sel4runtime/include/sel4_arch/x86_64 \
	-I$(ROOT)/build/sdk/sel4runtime/gen_config

FACET_INCLUDES := \
	-I$(ROOT)/include


#
# FacetOS dominit0 objects.
#
# Sources live in src/dominit0/.
#

FACET_DOMINIT0_SRCS := $(wildcard $(ROOT)/src/dominit0/*.c)

FACET_DOMINIT0_OBJS := \
	$(patsubst $(ROOT)/src/dominit0/%.c,$(FACET_DOMINIT0_BUILD)/%.o,$(FACET_DOMINIT0_SRCS))

FACET_DOMINIT0_DEPS := $(FACET_DOMINIT0_OBJS:.o=.d)


.PHONY: \
	all \
	venv \
	patches \
	sdk \
	facetos \
	bootstub32 \
	image \
	run \
	run-iso \
	facet-clean \
	bootstub32-clean \
	sdk-clean \
	full-clean


all: facetos


#
# Python environment used by the upstream seL4 build.
#

venv:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --upgrade pip setuptools
	$(VENV)/bin/pip install sel4-deps


#
# Patch nonsense.
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
# FacetOS build directories.
#

$(FACET_BUILD):
	mkdir -p $(FACET_BUILD)

$(FACET_DOMINIT0_BUILD): | $(FACET_BUILD)
	mkdir -p $(FACET_DOMINIT0_BUILD)


#
# Compile dominit0 source modules.
#
# Any src/dominit0/foo.c listed as:
#
#     $(FACET_DOMINIT0_BUILD)/foo.o
#
# in FACET_DOMINIT0_OBJS is automatically compiled by this rule.
#

$(FACET_DOMINIT0_BUILD)/%.o: src/dominit0/%.c | $(FACET_DOMINIT0_BUILD)
	$(CC) \
		$(FACET_CFLAGS) \
		$(SEL4_INCLUDES) \
		$(FACET_INCLUDES) \
		-c $< \
		-o $@


#
# Automatically generated header dependencies.
#
# -MMD generates a .d file alongside each .o.
# -MP prevents removed headers from causing stale dependency errors.
#

-include $(FACET_DOMINIT0_DEPS)


#
# Link dominit0.
#
# sel4runtime contains its own CRT/startup objects. Since they live
# inside a static archive, explicitly requiring _sel4_start causes
# the linker to extract the startup path from libsel4runtime.a.
#
# crti.o and crtn.o must surround the linked objects/libraries so
# the init/fini sections are constructed correctly.
#

$(FACET_DOMINIT0): $(FACET_DOMINIT0_OBJS)
	$(CC) \
		-nostdlib \
		-static \
		-no-pie \
		-Wl,-u,_sel4_start \
		-Wl,-e,_sel4_start \
		-Wl,-T,$(ROOT)/external/seL4_tools/cmake-tool/helpers/tls_rootserver.lds \
		-o $@ \
		$(ROOT)/build/sdk/lib/crti.o \
		$(FACET_DOMINIT0_OBJS) \
		-Wl,--start-group \
		$(ROOT)/build/sdk/libsel4/libsel4.a \
		$(ROOT)/build/sdk/sel4runtime/libsel4runtime.a \
		-Wl,--end-group \
		$(ROOT)/build/sdk/lib/crtn.o

	@echo
	@echo "FacetOS dominit0 linked successfully."
	@echo


facetos: $(FACET_DOMINIT0)


#
# bootstub32.
#
# Pass DEBUG through so:
#
#     make DEBUG=1
#
# builds both dominit0 and bootstub32 with their respective debug
# settings enabled.
#

bootstub32:
	$(MAKE) -C $(BOOTSTUB32_DIR) DEBUG=$(DEBUG)


#
# QEMU direct boot.
#
# QEMU loads bootstub32 as its Multiboot kernel. The first initrd module
# is the real seL4 ELF; bootstub32 consumes it and passes the remaining
# modules to seL4.
#

run: facetos bootstub32
	qemu-system-x86_64 \
		-enable-kvm \
		-cpu host \
		-m 512M \
		-kernel $(BOOTSTUB32) \
		-initrd $(SDK_BUILD)/kernel/kernel.elf,$(FACET_DOMINIT0) \
		-serial stdio


#
# GRUB ISO.
#
# Keep this as the conventional/reference boot path.
#

ISO_ROOT    := $(ROOT)/build/iso
FACETOS_ISO := $(ROOT)/build/facetos.iso


image: facetos
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub

	cp $(SDK_BUILD)/kernel/kernel.elf \
		$(ISO_ROOT)/boot/kernel.elf

	cp $(FACET_DOMINIT0) \
		$(ISO_ROOT)/boot/dominit0

	cp $(ROOT)/boot/grub.cfg \
		$(ISO_ROOT)/boot/grub/grub.cfg

	grub-mkrescue \
		-o $(FACETOS_ISO) \
		$(ISO_ROOT)


run-iso: image
	qemu-system-x86_64 \
		-enable-kvm \
		-cpu host \
		-m 512M \
		-cdrom $(FACETOS_ISO) \
		-serial stdio


#
# Cleaning.
#

facet-clean:
	rm -rf $(FACET_BUILD)
	rm -rf $(ISO_ROOT)
	rm -rf $(FACETOS_ISO)


bootstub32-clean:
	$(MAKE) -C $(BOOTSTUB32_DIR) clean


sdk-clean:
	rm -rf $(SDK_BUILD)
	rm -rf $(SDK)
	rm -rf $(SEL4_RUNTIME)/.facetos-patched
	git -C $(SEL4_RUNTIME) reset --hard


full-clean: sdk-clean facet-clean bootstub32-clean
	rm -rf $(ROOT)/sdk
	rm -rf $(ROOT)/build
