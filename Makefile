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
	-MMD \
	-MP

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
# FacetOS root task objects.
#

FACET_OBJS := \
	$(FACET_BUILD)/klock.o \
	$(FACET_BUILD)/klog.o \
	$(FACET_BUILD)/init.o

FACET_DEPS := $(FACET_OBJS:.o=.d)


.PHONY: all venv patches sdk facetos run clean sdk-clean image


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
# Compile FacetOS source modules.
#
# Any src/foo.c listed as build/facetos/foo.o in FACET_OBJS
# is automatically compiled by this rule.
#

$(FACET_BUILD)/%.o: src/%.c | $(FACET_BUILD)
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

-include $(FACET_DEPS)


#
# Link the FacetOS root task.
#
# sel4runtime contains its own CRT/startup objects. Since they live
# inside a static archive, explicitly requiring _sel4_start causes
# the linker to extract the startup path from libsel4runtime.a.
#

$(FACET_INIT): $(FACET_OBJS)
	$(CC) \
		-nostdlib \
		-static \
		-no-pie \
		-Wl,-u,_sel4_start \
		-Wl,-e,_sel4_start \
		-Wl,-T,$(ROOT)/external/seL4_tools/cmake-tool/helpers/tls_rootserver.lds \
		-o $@ \
		$(ROOT)/build/sdk/lib/crti.o \
		$(FACET_OBJS) \
		-Wl,--start-group \
		$(ROOT)/build/sdk/libsel4/libsel4.a \
		$(ROOT)/build/sdk/sel4runtime/libsel4runtime.a \
		-Wl,--end-group \
		$(ROOT)/build/sdk/lib/crtn.o

	@echo
	@echo "FacetOS init linked successfully."
	@echo


facetos: $(FACET_INIT)


#
# QEMU & GRUB etc
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
