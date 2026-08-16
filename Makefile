# FacetOS top-level Makefile

VENV        := $(CURDIR)/.venv

SEL4_SRC    := $(CURDIR)/external/seL4
SEL4_BUILD  := $(CURDIR)/build/sel4
SEL4_SDK    := $(CURDIR)/sdk/sel4
SEL4_CONFIG := $(SEL4_SRC)/configs/X64_verified.cmake

PYTHON      := $(VENV)/bin/python3
PIP         := $(VENV)/bin/pip
SEL4_ENV    := PATH="$(VENV)/bin:$(PATH)"

.PHONY: all venv sel4-kernel sel4-clean clean

all:
	@echo "FacetOS userspace build not implemented yet."

#
# Create the Python environment needed by the seL4 build tools.
#
venv:
	python3 -m venv $(VENV)
	$(PIP) install --upgrade pip setuptools
	$(PIP) install sel4-deps

#
# Build and install the standalone seL4 kernel/SDK.
#
sel4-kernel:
	$(SEL4_ENV) cmake \
		-G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(SEL4_SRC)/gcc.cmake \
		-C $(SEL4_CONFIG) \
		-DCMAKE_INSTALL_PREFIX=$(SEL4_SDK) \
		-S $(SEL4_SRC) \
		-B $(SEL4_BUILD)

	$(SEL4_ENV) ninja \
		-C $(SEL4_BUILD) \
		kernel.elf

	$(SEL4_ENV) cmake \
		--install $(SEL4_BUILD)

#
# Remove seL4 build products and installed SDK.
#
sel4-clean:
	rm -rf $(SEL4_BUILD)
	rm -rf $(SEL4_SDK)

#
# Eventually this will remove FacetOS's own build products,
# but deliberately leaves the expensive seL4 SDK alone.
#
clean:
	rm -rf build/facetos
