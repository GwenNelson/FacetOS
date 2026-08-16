cmake_minimum_required(VERSION 3.16)

project(FacetOS C ASM)

set(KernelArch x86 CACHE STRING "")
set(KernelX86MicroArch generic CACHE STRING "")
set(KernelSel4Arch x86_64 CACHE STRING "")
set(KernelPlatform pc99 CACHE STRING "")

set(KernelDebugBuild ON CACHE BOOL "")
set(KernelPrinting ON CACHE BOOL "")
set(KernelVerificationBuild OFF CACHE BOOL "")

find_package(seL4 REQUIRED
    PATHS "${CMAKE_SOURCE_DIR}/external/seL4"
)

sel4_import_kernel()
sel4_import_libsel4()

add_executable(facet-init
    src/init.c
)

target_link_libraries(facet-init
    PRIVATE
    sel4
)

include(rootserver)
DeclareRootserver(facet-init)
