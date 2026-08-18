#
# FacetOS
#

.DEFAULT_GOAL := all

ROOT := $(CURDIR)

VENV := $(ROOT)/.venv

SEL4_SRC     := $(ROOT)/external/seL4
SEL4_RUNTIME := $(ROOT)/external/sel4runtime

SDK_BUILD := $(ROOT)/build/sdk
SDK       := $(ROOT)/sdk
SEL4_SDK  := $(SDK)/sel4

FACET_BUILD := $(ROOT)/build/facetos

BOOTSTUB32_DIR := $(ROOT)/bootstub32
BOOTSTUB32     := $(BOOTSTUB32_DIR)/bootstub32

SEL4_CONFIG := $(ROOT)/config/FacetOS-seL4.cmake

SEL4_ENV := PATH="$(VENV)/bin:$(PATH)"

CC := gcc
AR := ar


#
# FacetOS compiler settings.
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
# FacetOS component system.
#
# Register each native component ONCE in COMPONENTS and declare it below.
#
# Every component gets:
#
#     FACET_<NAME>_DIR
#     FACET_<NAME>_BUILD
#     FACET_<NAME>_SRCS
#     FACET_<NAME>_OBJS
#     FACET_<NAME>_DEPS
#     FACET_<NAME>_CFLAGS
#
# The generic component macro also creates:
#
#     build directory rule
#     src/<dir>/*.c -> build/facetos/<dir>/*.o rule
#
# $(1) = uppercase component name
# $(2) = source/build directory name
#

COMPONENTS :=


define FACET_COMPONENT
COMPONENTS += $(1)

FACET_$(1)_DIR := $(2)
FACET_$(1)_BUILD := $$(FACET_BUILD)/$(2)

FACET_$(1)_SRCS := \
	$$(wildcard $$(ROOT)/src/$(2)/*.c)

FACET_$(1)_OBJS := \
	$$(patsubst \
		$$(ROOT)/src/$(2)/%.c, \
		$$(FACET_$(1)_BUILD)/%.o, \
		$$(FACET_$(1)_SRCS))

FACET_$(1)_DEPS := \
	$$(FACET_$(1)_OBJS:.o=.d)

FACET_$(1)_CFLAGS ?=
FACET_$(1)_LIBS ?=

$$(FACET_$(1)_BUILD):
	mkdir -p $$@

$$(FACET_$(1)_BUILD)/%.o: $$(ROOT)/src/$(2)/%.c | $$(FACET_$(1)_BUILD)
	$$(CC) \
		$$(FACET_CFLAGS) \
		$$(FACET_$(1)_CFLAGS) \
		$$(SEL4_INCLUDES) \
		$$(FACET_INCLUDES) \
		-c $$< \
		-o $$@
endef


#
# Static-library component.
#
# Adds:
#
#     FACET_<NAME>_TARGET
#
# and generates the archive rule and a lowercase-named phony target.
#
# $(1) = uppercase component name
# $(2) = source/build directory
# $(3) = output basename, without .a
# $(4) = phony target name
#

define FACET_STATIC_LIBRARY
$$(eval $$(call FACET_COMPONENT,$(1),$(2)))

FACET_$(1)_TARGET := \
	$$(FACET_$(1)_BUILD)/$(3).a

$$(FACET_$(1)_TARGET): $$(FACET_$(1)_OBJS)
	$$(AR) rcs $$@ $$^

	@echo
	@echo "FacetOS $(3).a built successfully."
	@echo

.PHONY: $(4)
$(4): $$(FACET_$(1)_TARGET)
endef


#
# Generic executable component.
#
# This is for ordinary executables whose link does not require a bespoke
# startup sequence.  Special executables such as dominit0 can still use
# FACET_COMPONENT and provide their own final link rule.
#
# Adds:
#
#     FACET_<NAME>_TARGET
#     FACET_<NAME>_LDFLAGS
#     FACET_<NAME>_LIBS
#
# $(1) = uppercase component name
# $(2) = source/build directory
# $(3) = output filename
# $(4) = phony target name
#

define FACET_EXECUTABLE
$$(eval $$(call FACET_COMPONENT,$(1),$(2)))

FACET_$(1)_TARGET := \
	$$(FACET_$(1)_BUILD)/$(3)

FACET_$(1)_LDFLAGS ?=

$$(FACET_$(1)_TARGET): $$(FACET_$(1)_OBJS) $$(FACET_$(1)_LIBS)
	$$(CC) \
		$$(FACET_$(1)_LDFLAGS) \
		-o $$@ \
		$$(FACET_$(1)_OBJS) \
		$$(FACET_$(1)_LIBS)

	@echo
	@echo "FacetOS $(3) linked successfully."
	@echo

.PHONY: $(4)
$(4): $$(FACET_$(1)_TARGET)
endef


#
# Component declarations.
#

$(eval $(call FACET_STATIC_LIBRARY,KLIBC,klibc,klibc,klibc))
$(eval $(call FACET_COMPONENT,DOMINIT0,dominit0))

FACET_DOMINIT0_TARGET := $(FACET_DOMINIT0_BUILD)/dominit0

#
# Optional per-component compiler flags.
#
# Example:
#
#     FACET_DOMINIT0_CFLAGS += -DFOO
#
# These are added after FACET_CFLAGS and therefore apply only to the named
# component's translation units.
#

FACET_KLIBC_CFLAGS    :=
FACET_DOMINIT0_CFLAGS :=

#
# Per-component library dependencies.
#
# List actual library targets here.  These are both Make prerequisites
# and linker inputs for components whose link rule consumes FACET_<NAME>_LIBS.
#

FACET_DOMINIT0_LIBS := \
	$(FACET_KLIBC_TARGET)


#
# Automatically generated dependency files.
#
# This is derived from COMPONENTS, so adding another registered component
# automatically adds its generated .d files here.
#

FACET_DEPS := \
	$(foreach component,$(COMPONENTS),$(FACET_$(component)_DEPS))

-include $(FACET_DEPS)


#
# Top-level native build outputs.
#

FACET_NATIVE_TARGETS := \
	$(FACET_KLIBC_TARGET) \
	$(FACET_DOMINIT0_TARGET)


.PHONY: \
	all \
	venv \
	patches \
	sdk \
	facetos \
	dominit0 \
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


#
# Patches.
#

SEL4RUNTIME_GCC16_PATCH := \
	$(ROOT)/patches/sel4runtime-gcc16.patch


patches:
	@if git -C $(SEL4_RUNTIME) apply \
		--reverse --check \
		$(SEL4RUNTIME_GCC16_PATCH) >/dev/null 2>&1; then \
		echo "sel4runtime GCC 16 patch already applied."; \
	else \
		echo "Applying sel4runtime GCC 16 patch..."; \
		git -C $(SEL4_RUNTIME) apply \
			$(SEL4RUNTIME_GCC16_PATCH); \
	fi



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

	$(SEL4_ENV) ninja -C $(SDK_BUILD) \
		kernel.elf \
		sel4runtime

	$(SEL4_ENV) cmake --install $(SDK_BUILD)


#
# dominit0.
#
# Keep this final link deliberately special for now.
#
# dominit0 is the seL4 root task and needs:
#
#     - sel4runtime startup
#     - special entry point
#     - special linker script
#     - CRT ordering
#     - seL4 runtime libraries
#
# klibc.a is linked as a normal FacetOS dependency.
#

$(FACET_DOMINIT0_TARGET): \
	$(FACET_DOMINIT0_OBJS) \
	$(FACET_DOMINIT0_LIBS)

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
		$(FACET_DOMINIT0_LIBS) \
		$(ROOT)/build/sdk/libsel4/libsel4.a \
		$(ROOT)/build/sdk/sel4runtime/libsel4runtime.a \
		-Wl,--end-group \
		$(ROOT)/build/sdk/lib/crtn.o

	@echo
	@echo "FacetOS dominit0 linked successfully."
	@echo


dominit0: $(FACET_DOMINIT0_TARGET)


#
# Complete native FacetOS build.
#

facetos: $(FACET_NATIVE_TARGETS)


#
# bootstub32.
#
# DEBUG is passed through so:
#
#     make DEBUG=1
#
# enables the bootstub's own debug configuration.
#

bootstub32:
	$(MAKE) -C $(BOOTSTUB32_DIR) DEBUG=$(DEBUG)


#
# ISO image contents.
#
# Each logical entry gets:
#
#     ISO_<NAME>_SRC
#     ISO_<NAME>_DST
#
# Add the entry name to ISO_FILES and the generic population rule handles it.
#

ISO_ROOT    := $(ROOT)/build/iso
FACETOS_ISO := $(ROOT)/build/facetos.iso

ISO_FILES := \
	KERNEL \
	DOMINIT0 \
	GRUBCFG

ISO_KERNEL_SRC := $(SDK_BUILD)/kernel/kernel.elf
ISO_KERNEL_DST := boot/kernel.elf

ISO_DOMINIT0_SRC := $(FACET_DOMINIT0_TARGET)
ISO_DOMINIT0_DST := boot/dominit0

ISO_GRUBCFG_SRC := $(ROOT)/boot/grub.cfg
ISO_GRUBCFG_DST := boot/grub/grub.cfg


#
# Generate one ordinary Make target for each file installed into the ISO tree.
# This means source timestamps work normally and adding another ISO file only
# requires adding its logical name plus SRC/DST metadata above.
#

define ISO_FILE_RULE
ISO_$(1)_TARGET := $$(ISO_ROOT)/$$(ISO_$(1)_DST)

$$(ISO_$(1)_TARGET): $$(ISO_$(1)_SRC)
	mkdir -p $$(dir $$@)
	cp $$< $$@
endef

$(foreach file,$(ISO_FILES),$(eval $(call ISO_FILE_RULE,$(file))))

ISO_TARGETS := \
	$(foreach file,$(ISO_FILES),$(ISO_$(file)_TARGET))


image: facetos $(ISO_TARGETS)
	grub-mkrescue \
		-o $(FACETOS_ISO) \
		$(ISO_ROOT)



#
# QEMU.
#
# Keep machine-wide settings in QEMU_FLAGS.
# Individual run targets append only the boot mechanism they need.
#

QEMU := qemu-system-x86_64

QEMU_FLAGS := \
	-enable-kvm \
	-cpu host \
	-m 512M \
	-serial stdio

ifeq ($(QEMU_GDB),1)
	QEMU_FLAGS += -S -s
endif


QEMU_DIRECT_FLAGS := \
	-kernel $(BOOTSTUB32) \
	-initrd $(SDK_BUILD)/kernel/kernel.elf,$(FACET_DOMINIT0_TARGET)

QEMU_ISO_FLAGS := \
	-cdrom $(FACETOS_ISO)


run: facetos bootstub32
	$(QEMU) \
		$(QEMU_FLAGS) \
		$(QEMU_DIRECT_FLAGS)


run-iso: image
	$(QEMU) \
		$(QEMU_FLAGS) \
		$(QEMU_ISO_FLAGS)


#
# Cleaning.
#
# Native FacetOS components all live below FACET_BUILD, so one removal
# automatically covers every component listed in COMPONENTS.
#
# External/sub-build systems register their clean target in
# EXTERNAL_CLEAN_TARGETS.
#

facet-clean:
	rm -rf $(FACET_BUILD)
	rm -rf $(ISO_ROOT)
	rm -f $(FACETOS_ISO)


bootstub32-clean:
	$(MAKE) -C $(BOOTSTUB32_DIR) clean


sdk-clean:
	rm -rf $(SDK_BUILD)
	rm -rf $(SDK)
	git -C $(SEL4_RUNTIME) reset --hard


EXTERNAL_CLEAN_TARGETS := \
	sdk-clean \
	bootstub32-clean


full-clean: facet-clean $(EXTERNAL_CLEAN_TARGETS)
	rm -rf $(ROOT)/build
	rm -rf $(ROOT)/sdk
