#
# FacetOS
#

.DEFAULT_GOAL := all

ROOT := $(CURDIR)

VENV := $(ROOT)/.venv

SEL4_SRC     := $(ROOT)/external/seL4
SEL4_RUNTIME := $(ROOT)/external/sel4runtime

SDK_BUILD := $(ROOT)/build/sdk

FACET_BUILD := $(ROOT)/build/facetos

BOOTSTUB32_DIR := $(ROOT)/bootstub32
BOOTSTUB32     := $(BOOTSTUB32_DIR)/bootstub32

SEL4_CONFIG := $(ROOT)/config/FacetOS-seL4.cmake

SEL4_ENV := PATH="$(VENV)/bin:$(PATH)"

SDK_KERNEL     := $(SDK_BUILD)/kernel/kernel.elf
FACET_DOMINIT0 := $(SDK_BUILD)/dominit0

ifeq ($(DEBUG),1)
	FACETOS_CMAKE_DEBUG := ON
else
	FACETOS_CMAKE_DEBUG := OFF
endif


.PHONY: \
	all \
	venv \
	patches \
	configure \
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
	full-clean \
	check-sdk


all: facetos


#
# Python environment used by the upstream seL4 build.
#

venv:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --upgrade pip setuptools
	$(VENV)/bin/pip install sel4-deps


#
# GCC 16 sel4runtime patch.
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
# Configure the complete seL4 + FacetOS userspace build universe.
#

configure: patches
	mkdir -p $(SDK_BUILD)

	$(SEL4_ENV) cmake \
		-G Ninja \
		-C $(SEL4_CONFIG) \
		-DCMAKE_TOOLCHAIN_FILE=$(SEL4_SRC)/gcc.cmake \
		-DFACETOS_DEBUG=$(FACETOS_CMAKE_DEBUG) \
		-S $(ROOT)/cmake/sdk \
		-B $(SDK_BUILD)


#
# Build the seL4 substrate plus FacetOS root task.
#

sdk: configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) \
		kernel.elf \
		dominit0


#
# Normal FacetOS userspace build.
#
# CMake owns the actual dominit0 build so it inherits the complete
# seL4/musl/sel4runtime build environment and transitive dependencies.
#

dominit0: configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) dominit0

facetos: dominit0


#
# Sanity checks.
#

check-sdk:
	@test -f $(SDK_KERNEL) || \
		{ echo "seL4 kernel missing; run 'make sdk' first."; exit 1; }
	@test -f $(FACET_DOMINIT0) || \
		{ echo "FacetOS dominit0 missing; run 'make' first."; exit 1; }


#
# bootstub32.
#

bootstub32:
	$(MAKE) -C $(BOOTSTUB32_DIR) DEBUG=$(DEBUG)


#
# ISO image contents.
#

ISO_ROOT    := $(ROOT)/build/iso
FACETOS_ISO := $(ROOT)/build/facetos.iso

ISO_FILES := \
	KERNEL \
	DOMINIT0 \
	GRUBCFG

ISO_KERNEL_SRC := $(SDK_KERNEL)
ISO_KERNEL_DST := boot/kernel.elf

ISO_DOMINIT0_SRC := $(FACET_DOMINIT0)
ISO_DOMINIT0_DST := boot/dominit0

ISO_GRUBCFG_SRC := $(ROOT)/boot/grub.cfg
ISO_GRUBCFG_DST := boot/grub/grub.cfg


define ISO_FILE_RULE
ISO_$(1)_TARGET := $$(ISO_ROOT)/$$(ISO_$(1)_DST)

$$(ISO_$(1)_TARGET): $$(ISO_$(1)_SRC)
	mkdir -p $$(dir $$@)
	cp $$< $$@
endef

$(foreach file,$(ISO_FILES),$(eval $(call ISO_FILE_RULE,$(file))))

ISO_TARGETS := \
	$(foreach file,$(ISO_FILES),$(ISO_$(file)_TARGET))


image: facetos check-sdk $(ISO_TARGETS)
	grub-mkrescue \
		-o $(FACETOS_ISO) \
		$(ISO_ROOT)


#
# QEMU.
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
	-initrd $(SDK_KERNEL),$(FACET_DOMINIT0)

QEMU_ISO_FLAGS := \
	-cdrom $(FACETOS_ISO)


run: facetos check-sdk bootstub32
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

facet-clean:
	rm -rf $(FACET_BUILD)
	rm -rf $(ISO_ROOT)
	rm -f $(FACETOS_ISO)


bootstub32-clean:
	$(MAKE) -C $(BOOTSTUB32_DIR) clean


sdk-clean:
	rm -rf $(SDK_BUILD)
	git -C $(SEL4_RUNTIME) reset --hard


EXTERNAL_CLEAN_TARGETS := \
	sdk-clean \
	bootstub32-clean


full-clean: facet-clean $(EXTERNAL_CLEAN_TARGETS)
	rm -rf $(ROOT)/build
