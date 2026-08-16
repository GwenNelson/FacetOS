cmake_minimum_required(VERSION 3.16)

project(FacetOS_SDK C ASM)

set(SEL4_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/seL4")

set(SEL4RUNTIME_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/sel4runtime")

#
# Set up the basic seL4 build environment.
#
list(
    APPEND CMAKE_MODULE_PATH
    "${SEL4_SOURCE_DIR}"
)

find_package(seL4 REQUIRED)

sel4_import_kernel()
sel4_import_libsel4()

#
# Now sel4runtime can see:
#
#     sel4
#     sel4_autoconf
#     KernelArch
#     KernelSel4Arch
#     KernelWordSize
#
add_subdirectory(
    "${SEL4RUNTIME_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/sel4runtime"
)

target_compile_options(sel4runtime PRIVATE
    -fno-stack-protector
)

#
# FacetOS SDK exports
#

install(
    TARGETS sel4runtime
    ARCHIVE DESTINATION sel4runtime/lib
)

install(
    DIRECTORY "${SEL4RUNTIME_SOURCE_DIR}/include/"
    DESTINATION sel4runtime/include
)

install(
    FILES
        "${CMAKE_BINARY_DIR}/lib/crt0.o"
        "${CMAKE_BINARY_DIR}/lib/crti.o"
        "${CMAKE_BINARY_DIR}/lib/crtn.o"
    DESTINATION sel4runtime/lib
)

#
# Export seL4 kernel
#

install(
    FILES "${CMAKE_BINARY_DIR}/kernel/kernel.elf"
    DESTINATION sel4/bin
)

#
# Export libsel4 headers
#

install(
    DIRECTORY
        "${SEL4_SOURCE_DIR}/libsel4/include/"
        "${SEL4_SOURCE_DIR}/libsel4/arch_include/x86/"
        "${SEL4_SOURCE_DIR}/libsel4/sel4_arch_include/x86_64/"
        "${SEL4_SOURCE_DIR}/libsel4/sel4_plat_include/pc99/"
        "${SEL4_SOURCE_DIR}/libsel4/mode_include/64/"
        "${CMAKE_BINARY_DIR}/libsel4/include/"
        "${CMAKE_BINARY_DIR}/libsel4/arch_include/x86/"
        "${CMAKE_BINARY_DIR}/libsel4/sel4_arch_include/x86_64/"
        "${CMAKE_BINARY_DIR}/libsel4/autoconf/"
        "${CMAKE_BINARY_DIR}/kernel/gen_config/"
        "${CMAKE_BINARY_DIR}/libsel4/gen_config/"
    DESTINATION sel4/libsel4/include
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.json"
        PATTERN "*.xml"
        PATTERN "*.xsd"
)

install(
    DIRECTORY "${SEL4_SOURCE_DIR}/libsel4/src/"
    DESTINATION sel4/libsel4/src
)
