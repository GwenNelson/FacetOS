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
FACET_SHELL    := $(SDK_BUILD)/FacetShell
FACET_LOGIN    := $(SDK_BUILD)/FacetLogin
FACET_DUMMY    := $(SDK_BUILD)/FacetDummy
FACET_DUMMYSH  := $(SDK_BUILD)/dummysh
FACET_POSIX_LOGIN := $(SDK_BUILD)/facet-posix/PosixLogin
FACET_SEAT_SERIAL := $(SDK_BUILD)/seat-server-serial
FACET_SEAT_PC := $(SDK_BUILD)/seat-server-pc-console
FACET_CONFIG_FILE := $(ROOT)/config/facet.toml
INITRD_SYSTEM := $(ROOT)/build/initrd/system.initrd
INITRD_CHILD := $(ROOT)/build/initrd/child.initrd
INITRD_DOMINIT0 := $(ROOT)/build/initrd/dominit0.initrd

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

FACET_COMMON_BUILD := $(ROOT)/build/facetos/libfacet/common
FACET_COMMON       := $(FACET_COMMON_BUILD)/libfacet-common.a

FACET_CONFIG_BUILD := $(ROOT)/build/facetos/config
FACET_CONFIG       := $(FACET_CONFIG_BUILD)/libfacet-config.a

FACET_IDLC_BUILD := $(ROOT)/build/facet-idlc
FACET_IDLC       := $(FACET_IDLC_BUILD)/facet-idlc

FACET_GENERATED_INCLUDE := $(ROOT)/build/facetos/generated/include
FACET_GENERATED_INTERFACE_DIR := $(FACET_GENERATED_INCLUDE)/facetos/interfaces
FACET_GENERATED_IGENERIC := $(FACET_GENERATED_INTERFACE_DIR)/IGenericObject.h
FACET_GENERATED_ILOGGING := $(FACET_GENERATED_INTERFACE_DIR)/ILoggingConfig.h
FACET_GENERATED_ILOGGING_SINK := $(FACET_GENERATED_INTERFACE_DIR)/ILoggingSink.h
FACET_GENERATED_ICONSOLE := $(FACET_GENERATED_INTERFACE_DIR)/IDomainConsoleConfig.h
FACET_GENERATED_IDOMAIN_CONFIG := $(FACET_GENERATED_INTERFACE_DIR)/IDomainConfig.h
FACET_GENERATED_IPAGE_ALLOCATOR := $(FACET_GENERATED_INTERFACE_DIR)/IPageAllocator.h

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

FACET_COMMON_CFLAGS := \
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
	FACET_COMMON_CFLAGS += -Og -g
else
	FACET_COMMON_CFLAGS += -O2
endif

FACET_IDLC_CFLAGS := \
	-std=gnu11 \
	-Wall \
	-Wextra \
	-I$(ROOT)/include \
	-Isrc/facet-idlc \
	-I$(FACET_IDLC_BUILD) \
	-MMD \
	-MP

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
	check-sdk \
	facet-idlc \
	libfacet-common \
	libfacet-platform-sel4 \
	libfacet \
	facet-config \
	test \
	test-config \
	test-sha256 \
	test-initrd \
	test-klog \
	test-logging-setup \
	test-seat-pc-console


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

configure: patches facet-idlc
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
# libfacet-common, built independently of the seL4 CMake universe.
#

FACET_COMMON_SRCS := $(wildcard $(ROOT)/libfacet/src/common/*.c)
FACET_COMMON_OBJS := \
	$(patsubst $(ROOT)/libfacet/src/common/%.c,$(FACET_COMMON_BUILD)/%.o,$(FACET_COMMON_SRCS))
FACET_COMMON_DEPS := $(FACET_COMMON_OBJS:.o=.d)

$(FACET_COMMON_BUILD)/%.o: $(ROOT)/libfacet/src/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FACET_COMMON_CFLAGS) -c $< -o $@

$(FACET_COMMON): $(FACET_COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

libfacet-common: $(FACET_COMMON)

-include $(FACET_COMMON_DEPS)


#
# Portable typed configuration parser.
#

FACET_CONFIG_OBJS := $(FACET_CONFIG_BUILD)/config.o
FACET_CONFIG_DEPS := $(FACET_CONFIG_OBJS:.o=.d)

$(FACET_CONFIG_BUILD)/config.o: $(ROOT)/src/config.c \
		$(ROOT)/include/facetos/config.h
	@mkdir -p $(dir $@)
	$(CC) $(FACET_COMMON_CFLAGS) -c $< -o $@

$(FACET_CONFIG): $(FACET_CONFIG_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

facet-config: $(FACET_CONFIG)

-include $(FACET_CONFIG_DEPS)


#
# Host-side configuration tests.
#

FACET_CONFIG_TEST := $(FACET_CONFIG_BUILD)/config-test
FACET_CONFIG_OBJECTS_TEST := $(FACET_CONFIG_BUILD)/config-objects-test
FACET_KLOG_TEST := $(FACET_CONFIG_BUILD)/klog-test
FACET_LOGGING_SETUP_TEST := $(FACET_CONFIG_BUILD)/logging-setup-test
FACET_SHA256_TEST := $(FACET_CONFIG_BUILD)/sha256-test
FACET_INITRD_TEST := $(FACET_CONFIG_BUILD)/initrd-test
FACET_AUTH_RPC_TEST := $(FACET_CONFIG_BUILD)/auth-rpc-test
FACET_TERMINAL_RPC_TEST := $(FACET_CONFIG_BUILD)/terminal-rpc-test
FACET_SEAT_PC_CONSOLE_TEST := $(FACET_CONFIG_BUILD)/seat-pc-console-test

$(FACET_GENERATED_IGENERIC): $(FACET_IDLC) $(ROOT)/idl/IGenericObject.facet
	@mkdir -p $(FACET_GENERATED_INTERFACE_DIR)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/IGenericObject.facet

$(FACET_GENERATED_ILOGGING): $(FACET_IDLC) $(ROOT)/idl/ILoggingConfig.facet \
		$(FACET_GENERATED_IGENERIC)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/ILoggingConfig.facet

$(FACET_GENERATED_ILOGGING_SINK): $(FACET_IDLC) \
		$(ROOT)/idl/ILoggingSink.facet $(FACET_GENERATED_IGENERIC)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/ILoggingSink.facet

$(FACET_GENERATED_ICONSOLE): $(FACET_IDLC) \
		$(ROOT)/idl/IDomainConsoleConfig.facet $(FACET_GENERATED_IGENERIC)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/IDomainConsoleConfig.facet

$(FACET_GENERATED_IDOMAIN_CONFIG): $(FACET_IDLC) \
		$(ROOT)/idl/IDomainConfig.facet $(FACET_GENERATED_IGENERIC) \
		$(FACET_GENERATED_ILOGGING) $(FACET_GENERATED_ICONSOLE)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/IDomainConfig.facet

$(FACET_GENERATED_IPAGE_ALLOCATOR): $(FACET_IDLC) \
		$(ROOT)/idl/IPageAllocator.facet $(FACET_GENERATED_IGENERIC)
	$(FACET_IDLC) -o $@ $(ROOT)/idl/IPageAllocator.facet

$(FACET_CONFIG_TEST): $(ROOT)/tests/config_test.c $(ROOT)/src/config.c \
		$(ROOT)/include/facetos/config.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -I$(ROOT)/include \
		$(ROOT)/src/config.c $(ROOT)/tests/config_test.c -o $@

$(FACET_CONFIG_OBJECTS_TEST): $(ROOT)/tests/config_objects_test.c \
		$(ROOT)/src/config.c $(ROOT)/src/dominit0/config.c \
		$(ROOT)/include/facetos/config.h \
		$(ROOT)/include/facetos/dominit0/config.h \
		$(FACET_GENERATED_IDOMAIN_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror \
		-I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/src/config.c $(ROOT)/src/dominit0/config.c \
		$(ROOT)/tests/config_objects_test.c -Wl,--gc-sections -o $@

test-config: $(FACET_CONFIG_TEST) $(FACET_CONFIG_OBJECTS_TEST)
	$(FACET_CONFIG_TEST)
	$(FACET_CONFIG_OBJECTS_TEST)

$(FACET_SHA256_TEST): $(ROOT)/tests/sha256_test.c \
		$(ROOT)/libfacet/src/common/sha256.c \
		$(ROOT)/include/facetos/sha256.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -I$(ROOT)/include \
		$(ROOT)/libfacet/src/common/sha256.c \
		$(ROOT)/tests/sha256_test.c -o $@

test-sha256: $(FACET_SHA256_TEST)
	$(FACET_SHA256_TEST)

$(FACET_INITRD_TEST): $(ROOT)/tests/initrd_test.c \
		$(ROOT)/src/dominit0/initrd.c $(ROOT)/include/facetos/initrd.h \
		$(FACET_IDLC)
	@mkdir -p $(dir $@)
	$(SEL4_ENV) ninja -C $(SDK_BUILD) facet-idl-shell-contracts-generated
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/libfacet/src/common/runtime.c \
		$(ROOT)/src/dominit0/initrd.c $(ROOT)/tests/initrd_test.c \
		-Wl,--gc-sections -o $@

test-initrd: $(FACET_INITRD_TEST)
	$(FACET_INITRD_TEST)

$(FACET_AUTH_RPC_TEST): $(ROOT)/tests/auth_rpc_test.c \
		$(ROOT)/src/config.c $(ROOT)/src/dominit0/config.c \
		$(ROOT)/src/dominit0/environment.c $(ROOT)/src/dominit0/auth.c \
		$(ROOT)/libfacet/src/common/runtime.c \
		$(ROOT)/libfacet/src/common/sha256.c $(FACET_IDLC)
	@mkdir -p $(dir $@)
	$(SEL4_ENV) ninja -C $(SDK_BUILD) facet-idl-config-generated \
		facet-idl-environment-generated facet-idl-shell-contracts-generated
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/libfacet/src/common/runtime.c \
		$(ROOT)/libfacet/src/common/sha256.c $(ROOT)/src/config.c \
		$(ROOT)/src/dominit0/config.c $(ROOT)/src/dominit0/environment.c \
		$(ROOT)/src/dominit0/auth.c $(ROOT)/tests/auth_rpc_test.c \
		-Wl,--gc-sections -o $@

test-auth-rpc: $(FACET_AUTH_RPC_TEST)
	$(FACET_AUTH_RPC_TEST)

$(FACET_TERMINAL_RPC_TEST): $(ROOT)/tests/terminal_rpc_test.c \
		$(ROOT)/src/config.c $(ROOT)/src/dominit0/config.c \
		$(ROOT)/src/dominit0/environment.c \
		$(ROOT)/src/dominit0/process_environment.c \
		$(ROOT)/src/dominit0/posix.c \
		$(ROOT)/src/dominit0/terminal.c $(ROOT)/src/dominit0/initrd.c \
		$(ROOT)/libfacet/src/common/runtime.c \
		$(FACET_IDLC)
	@mkdir -p $(dir $@)
	$(SEL4_ENV) ninja -C $(SDK_BUILD) facet-idl-config-generated \
		facet-idl-environment-generated facet-idl-shell-contracts-generated
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/libfacet/src/common/runtime.c $(ROOT)/src/config.c \
		$(ROOT)/src/dominit0/config.c $(ROOT)/src/dominit0/environment.c \
		$(ROOT)/src/dominit0/process_environment.c \
		$(ROOT)/src/dominit0/posix.c \
		$(ROOT)/src/dominit0/terminal.c $(ROOT)/src/dominit0/initrd.c \
		$(ROOT)/tests/terminal_rpc_test.c \
		-Wl,--gc-sections -o $@

test-terminal-rpc: $(FACET_TERMINAL_RPC_TEST)
	$(FACET_TERMINAL_RPC_TEST)

$(FACET_SEAT_PC_CONSOLE_TEST): $(ROOT)/tests/seat_pc_console_test.c \
		$(ROOT)/src/seat/pc_console.c $(ROOT)/src/seat/pc_console.h \
		$(ROOT)/src/seat/pc_cursor.c $(ROOT)/src/seat/pc_cursor.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror \
		-I$(ROOT)/src/seat $(ROOT)/src/seat/pc_console.c \
		$(ROOT)/src/seat/pc_cursor.c \
		$(ROOT)/tests/seat_pc_console_test.c -o $@

test-seat-pc-console: $(FACET_SEAT_PC_CONSOLE_TEST)
	$(FACET_SEAT_PC_CONSOLE_TEST)

$(FACET_KLOG_TEST): $(ROOT)/tests/klog_test.c \
		$(ROOT)/src/dominit0/klog.c $(ROOT)/src/dominit0/klock.c \
		$(ROOT)/include/facetos/dominit0/klog.h \
		$(FACET_GENERATED_ILOGGING) $(FACET_GENERATED_ILOGGING_SINK)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror \
		-I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/src/dominit0/klog.c $(ROOT)/src/dominit0/klock.c \
		$(ROOT)/tests/klog_test.c -Wl,--gc-sections -o $@

test-klog: $(FACET_KLOG_TEST)
	$(FACET_KLOG_TEST)

$(FACET_LOGGING_SETUP_TEST): $(ROOT)/tests/logging_setup_test.c \
		$(ROOT)/src/dominit0/logging.c $(ROOT)/src/dominit0/klog.c \
		$(ROOT)/src/dominit0/klock.c \
		$(ROOT)/include/facetos/dominit0/logging.h \
		$(FACET_GENERATED_IDOMAIN_CONFIG) $(FACET_GENERATED_ILOGGING_SINK)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror \
		-I$(FACET_GENERATED_INCLUDE) -I$(ROOT)/include \
		$(ROOT)/src/dominit0/logging.c $(ROOT)/src/dominit0/klog.c \
		$(ROOT)/src/dominit0/klock.c $(ROOT)/tests/logging_setup_test.c \
		-Wl,--gc-sections -o $@

test-logging-setup: $(FACET_LOGGING_SETUP_TEST)
	$(FACET_LOGGING_SETUP_TEST) optional
	$(FACET_LOGGING_SETUP_TEST) required

test: test-config test-sha256 test-initrd test-auth-rpc test-terminal-rpc \
	test-klog test-logging-setup test-seat-pc-console


#
# Host-side Flex/Bison IDL compiler.
#

FACET_IDLC_STATIC_SRCS := \
	$(ROOT)/src/facet-idlc/ast.c \
	$(ROOT)/src/facet-idlc/generator.c \
	$(ROOT)/src/facet-idlc/main.c
FACET_IDLC_STATIC_OBJS := \
	$(patsubst $(ROOT)/src/facet-idlc/%.c,$(FACET_IDLC_BUILD)/%.o,$(FACET_IDLC_STATIC_SRCS))
FACET_IDLC_OBJS := \
	$(FACET_IDLC_STATIC_OBJS) \
	$(FACET_IDLC_BUILD)/parser.o \
	$(FACET_IDLC_BUILD)/lexer.o
FACET_IDLC_DEPS := $(FACET_IDLC_STATIC_OBJS:.o=.d)

$(FACET_IDLC_BUILD)/parser.c $(FACET_IDLC_BUILD)/parser.h: \
	$(ROOT)/src/facet-idlc/parser.y $(ROOT)/src/facet-idlc/ast.h
	@mkdir -p $(FACET_IDLC_BUILD)
	bison --defines=$(FACET_IDLC_BUILD)/parser.h \
		-o $(FACET_IDLC_BUILD)/parser.c $<

$(FACET_IDLC_BUILD)/lexer.c: \
	$(ROOT)/src/facet-idlc/lexer.l $(FACET_IDLC_BUILD)/parser.h
	@mkdir -p $(FACET_IDLC_BUILD)
	flex -o $@ $<

$(FACET_IDLC_BUILD)/%.o: $(ROOT)/src/facet-idlc/%.c \
	$(FACET_IDLC_BUILD)/parser.h
	@mkdir -p $(dir $@)
	$(CC) $(FACET_IDLC_CFLAGS) -c $< -o $@

$(FACET_IDLC_BUILD)/parser.o: $(FACET_IDLC_BUILD)/parser.c \
	$(ROOT)/src/facet-idlc/ast.h
	$(CC) $(FACET_IDLC_CFLAGS) -c $< -o $@

$(FACET_IDLC_BUILD)/lexer.o: $(FACET_IDLC_BUILD)/lexer.c \
	$(FACET_IDLC_BUILD)/parser.h
	$(CC) $(FACET_IDLC_CFLAGS) -c $< -o $@

$(FACET_IDLC): $(FACET_IDLC_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(FACET_IDLC_OBJS) -o $@

facet-idlc: $(FACET_IDLC)

-include $(FACET_IDLC_DEPS)


#
# Ninja-backed build targets.
#
# These are intentionally phony from Make's point of view. Make does not know
# the CMake/Ninja dependency graph, so it should always ASK Ninja whether a
# target needs rebuilding. An up-to-date Ninja invocation is essentially free.
#

kernel: configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) kernel.elf


dominit: facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) dominit

$(FACET_SHELL): facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) FacetShell

$(FACET_LOGIN): facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) FacetLogin

$(FACET_DUMMY) $(FACET_DUMMYSH): facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) FacetDummy dummysh


dominit0: klibc facet-idlc libfacet-common facet-config configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) dominit0 dominit


facetos: dominit0 libfacet


libfacet-platform-sel4: libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) facet-platform-sel4


libfacet: facet-idlc libfacet-common libfacet-platform-sel4


# Build everything needed to boot FacetOS.
build: klibc facet-config configure libfacet $(INITRD_DOMINIT0) $(INITRD_SYSTEM) $(INITRD_CHILD)
	$(SEL4_ENV) ninja -C $(SDK_BUILD) kernel.elf dominit0 dominit FacetLogin FacetShell FacetDummy dummysh seat-server-serial seat-server-pc-console

$(FACET_SEAT_SERIAL) $(FACET_SEAT_PC): facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) seat-server-serial seat-server-pc-console

$(INITRD_DOMINIT0): $(FACET_SEAT_SERIAL) $(FACET_SEAT_PC)
	mkdir -p $(dir $@)
	mkdir -p $(ROOT)/build/initrd/dominit0-root/FacetOS
	cp $(FACET_SEAT_SERIAL) $(ROOT)/build/initrd/dominit0-root/FacetOS/seat-server-serial
	cp $(FACET_SEAT_PC) $(ROOT)/build/initrd/dominit0-root/FacetOS/seat-server-pc-console
	cd $(ROOT)/build/initrd/dominit0-root && find . -print | sort | cpio --quiet -o -H newc > $@

$(INITRD_SYSTEM): $(ROOT)/initrd/system/README $(FACET_LOGIN) $(FACET_SHELL) $(FACET_DUMMY)
	mkdir -p $(dir $@)
	mkdir -p $(ROOT)/build/initrd/system-root/FacetOS
	cp $(ROOT)/initrd/system/README $(ROOT)/build/initrd/system-root/README
	cp $(FACET_LOGIN) $(ROOT)/build/initrd/system-root/FacetOS/FacetLogin
	cp $(FACET_SHELL) $(ROOT)/build/initrd/system-root/FacetOS/FacetShell
	cp $(FACET_DUMMY) $(ROOT)/build/initrd/system-root/FacetOS/FacetDummy
	cd $(ROOT)/build/initrd/system-root && find . -print | sort | cpio --quiet -o -H newc > $@

$(FACET_POSIX_LOGIN): facet-idlc libfacet-common configure
	$(SEL4_ENV) ninja -C $(SDK_BUILD) PosixLogin

$(INITRD_CHILD): $(ROOT)/initrd/child/README $(FACET_DUMMYSH) $(FACET_POSIX_LOGIN)
	mkdir -p $(dir $@)
	mkdir -p $(ROOT)/build/initrd/child-root/bin
	cp $(ROOT)/initrd/child/README $(ROOT)/build/initrd/child-root/README
	cp $(FACET_DUMMYSH) $(ROOT)/build/initrd/child-root/bin/dummysh
	cp $(FACET_POSIX_LOGIN) $(ROOT)/build/initrd/child-root/bin/login
	cd $(ROOT)/build/initrd/child-root && find . -print | sort | cpio --quiet -o -H newc > $@


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
	cp $(FACET_CONFIG_FILE) $(ISO_ROOT)/boot/facet.toml
	cp $(INITRD_DOMINIT0) $(ISO_ROOT)/boot/dominit0.initrd
	cp $(INITRD_SYSTEM) $(ISO_ROOT)/boot/system.initrd
	cp $(INITRD_CHILD) $(ISO_ROOT)/boot/child.initrd
	cp $(ROOT)/boot/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue \
		-o $(FACETOS_ISO) \
		$(ISO_ROOT)


#
# QEMU.
#

QEMU := qemu-system-x86_64
BOCHS_DEBUG_LOG := $(ROOT)/build/bochs-debug.log

QEMU_FLAGS := \
	-enable-kvm \
	-cpu host \
	-m 512M \
	-serial stdio \
	-debugcon file:$(BOCHS_DEBUG_LOG) \
	-global isa-debugcon.iobase=0xe9

# Optional diagnostics such as a monitor socket can be supplied without
# changing the normal bounded `make run` command.
QEMU_EXTRA_FLAGS ?=

ifeq ($(QEMU_GDB),1)
	QEMU_FLAGS += -S -s
endif

QEMU_DIRECT_FLAGS := \
	-kernel $(BOOTSTUB32) \
	-append "debug_port=0xe9" \
	-initrd $(SDK_KERNEL),$(FACET_DOMINIT0),$(FACET_DOMINIT),$(FACET_CONFIG_FILE),$(INITRD_DOMINIT0),$(INITRD_SYSTEM),$(INITRD_CHILD)

QEMU_ISO_FLAGS := \
	-cdrom $(FACETOS_ISO)


# One command now does the incremental kernel/dominit0/dominit build, incrementally
# builds bootstub32, and boots the result.
run: build bootstub32
	rm -f $(BOCHS_DEBUG_LOG)
	$(TIMEOUT) $(QEMU) \
		$(QEMU_FLAGS) \
		$(QEMU_EXTRA_FLAGS) \
		$(QEMU_DIRECT_FLAGS)




run-iso: image
	$(TIMEOUT) $(QEMU) \
		$(QEMU_FLAGS) \
		$(QEMU_EXTRA_FLAGS) \
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
