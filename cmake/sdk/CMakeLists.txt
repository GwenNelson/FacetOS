cmake_minimum_required(VERSION 3.16)

project(FacetOSSDK C ASM)

#
# FacetOS seL4 SDK build
#

add_compile_options(-fno-stack-protector)

set(SEL4_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/seL4"
    CACHE PATH "Path to seL4"
)

set(SEL4RUNTIME_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/sel4runtime"
    CACHE PATH "Path to sel4runtime"
)

set(SEL4_TOOLS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/seL4_tools"
    CACHE PATH "Path to seL4_tools"
)

set(MUSLLIBC_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/musllibc"
    CACHE PATH "Path to musllibc"
)

set(UTIL_LIBS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/util_libs"
    CACHE PATH "Path to util_libs"
)

set(SEL4_LIBS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/seL4_libs"
    CACHE PATH "Path to seL4_libs"
)

#
# Upstream CMake modules.
#

list(APPEND CMAKE_MODULE_PATH
    "${SEL4_DIR}"
    "${SEL4_TOOLS_DIR}/cmake-tool/helpers"
    "${SEL4RUNTIME_DIR}"
    "${MUSLLIBC_DIR}"
    "${UTIL_LIBS_DIR}"
    "${SEL4_LIBS_DIR}"
)

#
# Upstream packages.
#

find_package(seL4 REQUIRED)
find_package(sel4runtime REQUIRED)
find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

#
# Import kernel/runtime environment.
#

sel4_import_kernel()

#
# This sets up the libc/runtime environment expected by util_libs and
# seL4_libs, including standard C headers such as stdint.h, string.h,
# assert.h, etc.
#

musllibc_setup_build_environment_with_sel4runtime()

#
# Import libsel4 and the user-level library collections.
#

sel4_import_libsel4()

util_libs_import_libraries()
sel4_libs_import_libraries()
