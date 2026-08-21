#
# FacetOS top-level build orchestrator
#
# CMake/Ninja owns the seL4 userspace build graph.
# Make provides the convenient human-facing targets.
#

.DEFAULT_GOAL := all

ROOT := $(CURDIR)

VENV := $(ROOT)/.venv

SEL4_SRC     := $(ROOT)/external/seL4
SEL4_RUNTIME := $(ROOT)/external/sel4runtime

SDK_BUILD := $(ROOT)/build/sdk

BOOTSTUB32_DIR := $(ROOT)/bootstub32
BOOTSTUB32     := $(BOOTSTUB32_DIR)/bootstub32

SEL4_CONFIG := $(ROOT)/config/FacetOS-seL4.cmake
CMAKE_SOURCE := $(ROOT)/cmake/sdk
CMAKE_LISTS  := $(CMAKE_SOURCE)/CMakeLists.txt

SEL4_ENV := PATH="$(VENV)/bin:$(PATH)"

SDK_KERNEL     := $(SDK_BUILD)/kernel/kernel.elf
FACET_DOMINIT0 := $(SDK_BUILD)/dominit0
FACET_DOMINIT  := $(SDK_BUILD)/dominit

#
# FacetOS klibc.
#
# klibc is deliberately built outside the seL4/musl CMake environment.
# It is a freestanding FacetOS library and uses the host compiler's
# freestanding compiler headers (stddef.h, stdarg.h, etc.).
#

CC ?= gcc
AR ?= ar

FACET_KLIBC_BUILD := $(ROOT)/build/facetos/klibc
FACET_KLIBC       := $(FACET_KLIBC_BUILD)/klibc.a

FACET_KLIBC_SRCS := $(wildcard $(ROOT)/src/klibc/*.c)
FACET_KLIBC_OBJS := \
	$(patsubst $(ROOT)/src/klibc/%.c,$(FACET_KLIBC_BUILD)/%.o,$(FACET_KLIBC_SRCS))
FACET_KLIBC_DEPS := $(FACET_KLIBC_OBJS:.o=.d)

FACET_KLIBC_CFLAGS := \
	-std=gnu11 \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-mno-red-zone \
	-Wall \
	-Wextra \
	-I$(ROOT)/include \
	-MMD \
	-MP

ifeq ($(DEBUG),1)
	FACET_KLIBC_CFLAGS += -Og -g -DDEBUG
else
	FACET_KLIBC_CFLAGS += -O2
endif

#
# Files whose changes require an explicit CMake configure pass.
# Source-file changes do NOT belong here: Ninja/CMake CONFIGURE_DEPENDS
# tracks those itself.
#
CMAKE_CONFIG_INPUTS := \
	$(CMAKE_LISTS) \
	$(SEL4_CONFIG)

CMAKE_STATE := $(SDK_BUILD)/.facet-config

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
	dominit \
	dominit0 \
	klibc \
	kernel \
	build \
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
# GCC 16 sel4runtime patch & boot modules preservation
#

SEL4RUNTIME_GCC16_PATCH := \
	$(ROOT)/patches/sel4runtime-gcc16.patch

SEL4_PRESERVE_MODULES_PATCH := \
	$(ROOT)/patches/sel4-preserve-multiboot-mods.patch


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
	@if git -C $(SEL4_SRC) apply \
		--reverse --check \
		$(SEL4_PRESERVE_MODULES_PATCH) >/dev/null 2>&1; then \
		echo "seL4 multiboot modules patch already applied."; \
	else \
		echo "Applying seL4 multiboot modules patch..."; \
		git -C $(SEL4_SRC) apply \
			$(SEL4_PRESERVE_MODULES_PATCH); \
	fi


#
# Configure the seL4 + FacetOS CMake universe only when necessary.
#
# `configure` is deliberately a phony convenience target, but the shell
# below avoids rerunning CMake unless:
#
#   * build/sdk/build.ninja does not exist;
#   * the requested DEBUG setting changed; or
#   * one of the explicit CMake/config inputs changed.
#
# Ordinary .c/.h edits are left to Ninja's own dependency graph.
#

configure: patches
	@mkdir -p $(SDK_BUILD)
	@desired='FACETOS_DEBUG=$(FACETOS_CMAKE_DEBUG)'; \
	need_configure=0; \
	if [ ! -f '$(SDK_BUILD)/build.ninja' ]; then \
		need_configure=1; \
	fi; \
	if [ ! -f '$(CMAKE_STATE)' ] || \
	   [ "$$(cat '$(CMAKE_STATE)' 2>/dev/null)" != "$$desired" ]; then \
		need_configure=1; \
	fi; \
	for input in $(CMAKE_CONFIG_INPUTS); do \
		if [ "$$input" -nt '$(SDK_BUILD)/build.ninja' ]; then \
			need_configure=1; \
		fi; \
	done; \
	if [ $$need_configure -eq 1 ]; then \
		echo "Configuring FacetOS/seL4 build..."; \
		$(SEL4_ENV) cmake \
			-G Ninja \
			-C $(SEL4_CONFIG) \
			-DCMAKE_TOOLCHAIN_FILE=$(SEL4_SRC)/gcc.cmake \
			-DFACETOS_DEBUG=$(FACETOS_CMAKE_DEBUG) \
			-S $(CMAKE_SOURCE) \
			-B $(SDK_BUILD) && \
		printf '%s\n' "$$desired" > '$(CMAKE_STATE)'; \
	else \
		echo "CMake configuration is up to date."; \
	fi


#
# FacetOS klibc build.
#
# Keep this outside CMake so it remains independent of musl/seL4.
#

$(FACET_KLIBC_BUILD)/%.o: $(ROOT)/src/klibc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FACET_KLIBC_CFLAGS) -c $< -o $@

$(FACET_KLIBC): $(FACET_KLIBC_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

klibc: $(FACET_KLIBC)

-include $(FACET_KLIBC_DEPS)


#
# Ninja-backed build targets.
#
# These are intentionally phony from Make's point of view. Make does not know
# the CMake/Ninja dependency graph, so it should always ASK Ninja whether a
# target needs rebuilding. An up-to-date Ninja invocation is essentially free.
#

kernel: configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) kernel.elf


dominit: configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) dominit


dominit0: klibc configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) dominit0 dominit


facetos: dominit0


# Build everything needed to boot FacetOS.
build: klibc configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) kernel.elf dominit0 dominit


# Keep `make sdk` as a familiar name, but it no longer implies cleaning.
sdk: build


#
# Optional sanity check for scripts/manual use.
#

check-sdk:
	@test -f $(SDK_KERNEL) || \
		{ echo "seL4 kernel missing; run 'make sdk' first."; exit 1; }
	@test -f $(FACET_DOMINIT0) || \
		{ echo "FacetOS dominit0 missing; run 'make' first."; exit 1; }
	@test -f $(FACET_DOMINIT) || \
		{ echo "FacetOS dominit missing; run 'make' first."; exit 1; }


#
# bootstub32.
#
# Its own Makefile handles incremental rebuilding.
#

bootstub32:
	$(MAKE) -C $(BOOTSTUB32_DIR) DEBUG=$(DEBUG)


#
# ISO image.
#
# Re-populating the tiny ISO staging tree is cheap and avoids trying to make
# GNU Make duplicate Ninja's knowledge of when kernel.elf/dominit0 changed.
#

ISO_ROOT    := $(ROOT)/build/iso
FACETOS_ISO := $(ROOT)/build/facetos.iso

image: build
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(SDK_KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp $(FACET_DOMINIT0) $(ISO_ROOT)/boot/dominit0
	cp $(FACET_DOMINIT) $(ISO_ROOT)/boot/dominit
	cp $(ROOT)/boot/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
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
	-initrd $(SDK_KERNEL),$(FACET_DOMINIT0),$(FACET_DOMINIT)

QEMU_ISO_FLAGS := \
	-cdrom $(FACETOS_ISO)


# One command now does the incremental kernel/dominit0/dominit build, incrementally
# builds bootstub32, and boots the result.
run: build bootstub32
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
	rm -rf $(ROOT)/build/facetos
	rm -rf $(ISO_ROOT)
	rm -f $(FACETOS_ISO)


bootstub32-clean:
	$(MAKE) -C $(BOOTSTUB32_DIR) clean


sdk-clean:
	rm -rf $(SDK_BUILD)
	git -C $(SEL4_RUNTIME) reset --hard
	git -C $(SEL4_SRC) reset --hard


full-clean: facet-clean sdk-clean bootstub32-clean
	rm -rf $(ROOT)/build
