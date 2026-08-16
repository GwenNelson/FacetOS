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

#
# Upstream CMake modules
#
list(APPEND CMAKE_MODULE_PATH
    "${SEL4_DIR}"
    "${SEL4_TOOLS_DIR}/cmake-tool/helpers"
    "${SEL4RUNTIME_DIR}"
)

#
# Upstream packages
#
find_package(seL4 REQUIRED)
find_package(sel4runtime REQUIRED)

#
# Import everything through the upstream build machinery.
#
sel4_import_kernel()
sel4runtime_import_project()
sel4_import_libsel4()
