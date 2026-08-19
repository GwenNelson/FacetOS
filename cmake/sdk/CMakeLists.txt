cmake_minimum_required(VERSION 3.16)

project(FacetOSSDK C ASM)

#
# FacetOS seL4 build environment.
#

set(FACETOS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

set(SEL4_DIR
    "${FACETOS_ROOT}/external/seL4"
    CACHE PATH "Path to seL4"
)

set(SEL4RUNTIME_DIR
    "${FACETOS_ROOT}/external/sel4runtime"
    CACHE PATH "Path to sel4runtime"
)

set(SEL4_TOOLS_DIR
    "${FACETOS_ROOT}/external/seL4_tools"
    CACHE PATH "Path to seL4_tools"
)

set(MUSLLIBC_DIR
    "${FACETOS_ROOT}/external/musllibc"
    CACHE PATH "Path to musllibc"
)

set(UTIL_LIBS_DIR
    "${FACETOS_ROOT}/external/util_libs"
    CACHE PATH "Path to util_libs"
)

set(SEL4_LIBS_DIR
    "${FACETOS_ROOT}/external/seL4_libs"
    CACHE PATH "Path to seL4_libs"
)

option(FACETOS_DEBUG "Build FacetOS userspace with debug support" OFF)

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
find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

#
# Build environment.
#
# musllibc_setup_build_environment_with_sel4runtime() establishes the
# userspace C environment and imports sel4runtime for us.
#

sel4_import_kernel()
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()


#
# FacetOS klibc.
#

file(GLOB FACET_KLIBC_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/src/klibc/*.c"
)

add_library(facet_klibc STATIC
    ${FACET_KLIBC_SRCS}
)

target_include_directories(facet_klibc PUBLIC
    "${FACETOS_ROOT}/include"
)

target_compile_options(facet_klibc PRIVATE
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -mno-red-zone
)

if(FACETOS_DEBUG)
    target_compile_options(facet_klibc PRIVATE -Og -g)
    target_compile_definitions(facet_klibc PRIVATE DEBUG)
else()
    target_compile_options(facet_klibc PRIVATE -O2)
endif()


#
# FacetOS dominit0.
#
# Generic dominit0 sources plus the selected seL4 platform backend.
#

file(GLOB FACET_DOMINIT0_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/src/dominit0/*.c"
    "${FACETOS_ROOT}/src/dominit0/platform/sel4/*.c"
)

add_executable(dominit0
    ${FACET_DOMINIT0_SRCS}
)

target_include_directories(dominit0 PRIVATE
    "${FACETOS_ROOT}/include"
)

target_compile_definitions(dominit0 PRIVATE
    FACET_PLATFORM_SEL4=1
)

target_compile_options(dominit0 PRIVATE
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -mno-red-zone
    -g
)

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE GCC_BUILTIN_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

target_include_directories(facet_klibc SYSTEM PRIVATE
    "${GCC_BUILTIN_INCLUDE_DIR}"
)

target_include_directories(dominit0 SYSTEM PRIVATE
    "${GCC_BUILTIN_INCLUDE_DIR}"
)

if(FACETOS_DEBUG)
    target_compile_options(dominit0 PRIVATE -Og -g)
    target_compile_definitions(dominit0 PRIVATE DEBUG)
else()
    target_compile_options(dominit0 PRIVATE -O2)
endif()

#
# Let upstream CMake targets carry their own transitive include paths,
# compile definitions, generated headers and library dependencies.
#
# libsel4allocman uses sel4utils internally, so include it explicitly as
# part of the root-task dependency set.
#

target_link_libraries(dominit0 PRIVATE
    facet_klibc

    sel4runtime
    sel4
    muslc
    utils

    sel4simple
    sel4simple-default
    sel4allocman
    sel4vka
    sel4utils
)

#
# Configure dominit0 as the initial seL4 userspace task.
#

include(rootserver)
DeclareRootserver(dominit0)
