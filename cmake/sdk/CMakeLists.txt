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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


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
# Common FacetOS userspace build settings.
#

function(facetos_configure_target target)
    target_include_directories(${target} PRIVATE
        "${FACETOS_ROOT}/include"
    )

    target_compile_options(${target} PRIVATE
        -ffreestanding
        -fno-stack-protector
        -fno-pic
        -mno-red-zone
    )

    if(FACETOS_DEBUG)
        target_compile_options(${target} PRIVATE -Og -g)
        target_compile_definitions(${target} PRIVATE DEBUG)
    else()
        target_compile_options(${target} PRIVATE -O2)
    endif()
endfunction()


#
# FacetOS klibc.
#
# CONFIGURE_DEPENDS makes Ninja/CMake notice newly-created or removed source
# files as well as ordinary edits to files already in the target.
#

file(GLOB FACET_KLIBC_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/src/klibc/*.c"
)

add_library(facet_klibc STATIC
    ${FACET_KLIBC_SRCS}
)

facetos_configure_target(facet_klibc)

# klibc headers are part of the FacetOS include surface for consumers.
target_include_directories(facet_klibc PUBLIC
    "${FACETOS_ROOT}/include"
)


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

facetos_configure_target(dominit0)

target_compile_definitions(dominit0 PRIVATE
    FACET_PLATFORM_SEL4=1
)


#
# Let upstream CMake targets carry their transitive include paths, generated
# configuration headers, compile definitions, and library dependencies.
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
    sel4vspace
    sel4utils
)


#
# Configure dominit0 as the initial seL4 userspace task.
#

include(rootserver)
DeclareRootserver(dominit0)
