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
