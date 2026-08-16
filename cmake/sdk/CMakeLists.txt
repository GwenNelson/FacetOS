cmake_minimum_required(VERSION 3.16)

project(FacetOS_SDK C ASM)

set(SEL4_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/seL4")

set(SEL4RUNTIME_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../external/sel4runtime")

#
# Bring the seL4 kernel/build environment into this project.
#
add_subdirectory(
    "${SEL4_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/seL4"
)

#
# sel4runtime now sees the architecture/configuration exported by seL4.
#
add_subdirectory(
    "${SEL4RUNTIME_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/sel4runtime"
)
