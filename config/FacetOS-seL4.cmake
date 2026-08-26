#!/usr/bin/env -S cmake -P
#
# FacetOS seL4 configuration
#
# Based on the seL4 x86_64 verified configuration, but with
# verification mode disabled so development/debug facilities
# such as kernel printing are available.
#

# If this file is executed directly, build the kernel.
include(${CMAKE_CURRENT_LIST_DIR}/../external/seL4/tools/helpers.cmake)
cmake_script_build_kernel(..)

# Platform / architecture
set(KernelPlatform "pc99" CACHE STRING "")
set(KernelSel4Arch "x86_64" CACHE STRING "")

# FacetOS development configuration
set(KernelVerificationBuild OFF CACHE BOOL "")
set(KernelDebugBuild ON CACHE BOOL "")
set(KernelPrinting ON CACHE BOOL "")

# Kernel configuration
set(KernelMaxNumNodes "1" CACHE STRING "")
set(KernelOptimisation "-O2" CACHE STRING "")
set(KernelRetypeFanOutLimit "256" CACHE STRING "")
set(KernelBenchmarks "none" CACHE STRING "")
set(KernelDangerousCodeInjection OFF CACHE BOOL "")
set(KernelFastpath ON CACHE BOOL "")
set(KernelNumDomains 16 CACHE STRING "")
set(KernelRootCNodeSizeBits 19 CACHE STRING "")
set(KernelMaxNumBootinfoUntypedCaps 230 CACHE STRING "")
set(KernelFSGSBase "msr" CACHE STRING "")
# Keep development images runnable under QEMU's TCG fallback as well as KVM.
# PCID is an optional TLB optimisation and TCG does not emulate it.
set(KernelSupportPCID OFF CACHE BOOL "" FORCE)
