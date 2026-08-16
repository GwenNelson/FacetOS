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
	-std=c11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-mno-red-zone \
	-I$(SEL4_SDK)/libsel4/include \
	-I$(SDK)/sel4runtime/include


.PHONY: all venv sdk facetos run clean sdk-clean


all: facetos


#
# Python environment used by the upstream seL4 build.
#

venv:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --upgrade pip setuptools
	$(VENV)/bin/pip install sel4-deps


#
# Build the seL4 + sel4runtime SDK.
#
# This is deliberately separate from normal FacetOS builds.
#

sdk:
	mkdir -p $(SDK_BUILD)
	mkdir -p $(SDK)

	$(SEL4_ENV) cmake \
		-G Ninja \
		-C $(SEL4_CONFIG) \
		-DCMAKE_TOOLCHAIN_FILE=$(SEL4_SRC)/gcc.cmake \
		-DCMAKE_INSTALL_PREFIX=$(SDK) \
		-S $(ROOT)/cmake/sdk \
		-B $(SDK_BUILD)

	$(SEL4_ENV) ninja -C $(SDK_BUILD)

	$(SEL4_ENV) cmake --install $(SDK_BUILD)


#
# FacetOS
#

$(FACET_BUILD):
	mkdir -p $(FACET_BUILD)


$(FACET_BUILD)/init.o: src/init.c | $(FACET_BUILD)
	$(CC) $(FACET_CFLAGS) \
		-c $< \
		-o $@


#
# Eventually this links:
#
#   crt0.o
#   crti.o
#   init.o
#   libsel4runtime.a
#   crtn.o
#
# The exact linker flags/entry point come from sel4runtime's CRT.
#

$(FACET_INIT): $(FACET_BUILD)/init.o
	@echo
	@echo "FacetOS init compiled successfully."
	@echo "Runtime linking is the next step."
	@echo
	@false


facetos: $(FACET_INIT)


#
# QEMU
#
# x86-64 seL4 requires an appropriate boot image/loader.
# We'll wire this to the elfloader image once that target exists.
#

run: facetos
	@echo "QEMU image generation not configured yet."
	@false


#
# Cleaning
#

clean:
	rm -rf $(FACET_BUILD)


sdk-clean:
	rm -rf $(SDK_BUILD)
	rm -rf $(SDK)

