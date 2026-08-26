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
# klibc is built by the top-level Makefile, deliberately outside the
# seL4/musl CMake environment.  CMake only imports the finished archive.
#

set(FACET_KLIBC_ARCHIVE
    "${FACETOS_ROOT}/build/facetos/klibc/klibc.a"
)

add_library(facet_klibc STATIC IMPORTED GLOBAL)

set_target_properties(facet_klibc PROPERTIES
    IMPORTED_LOCATION
        "${FACET_KLIBC_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${FACETOS_ROOT}/include"
)

set(FACET_COMMON_ARCHIVE
    "${FACETOS_ROOT}/build/facetos/libfacet/common/libfacet-common.a"
)

add_library(facet_common STATIC IMPORTED GLOBAL)

set_target_properties(facet_common PROPERTIES
    IMPORTED_LOCATION
        "${FACET_COMMON_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${FACETOS_ROOT}/include"
)

set(FACET_CONFIG_ARCHIVE
    "${FACETOS_ROOT}/build/facetos/config/libfacet-config.a"
)

add_library(facet_config STATIC IMPORTED GLOBAL)

set_target_properties(facet_config PROPERTIES
    IMPORTED_LOCATION
        "${FACET_CONFIG_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${FACETOS_ROOT}/include"
)

set(FACET_GENERATED_INCLUDE
    "${FACETOS_ROOT}/build/facetos/generated/include"
)
set(FACET_IDLC
    "${FACETOS_ROOT}/build/facet-idlc/facet-idlc"
)
set(FACET_GENERATED_IGENERIC
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IGenericObject.h"
)
set(FACET_GENERATED_IDEBUG
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IDebug.h"
)
set(FACET_GENERATED_ILOGGING_CONFIG
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/ILoggingConfig.h"
)
set(FACET_GENERATED_ILOGGING_SINK
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/ILoggingSink.h"
)
set(FACET_GENERATED_IDOMAIN_CONSOLE_CONFIG
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IDomainConsoleConfig.h"
)
set(FACET_GENERATED_IDOMAIN_CONFIG
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IDomainConfig.h"
)
set(FACET_GENERATED_IPAGE_ALLOCATOR
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IPageAllocator.h"
)
set(FACET_GENERATED_ILOGGER
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/ILogger.h"
)
set(FACET_GENERATED_IDOMAIN_ENVIRONMENT
    "${FACET_GENERATED_INCLUDE}/facetos/interfaces/IDomainEnvironment.h"
)

add_custom_command(
    OUTPUT
        "${FACET_GENERATED_IGENERIC}"
        "${FACET_GENERATED_IDEBUG}"
        "${FACET_GENERATED_ILOGGING_CONFIG}"
        "${FACET_GENERATED_ILOGGING_SINK}"
        "${FACET_GENERATED_IDOMAIN_CONSOLE_CONFIG}"
        "${FACET_GENERATED_IDOMAIN_CONFIG}"
        "${FACET_GENERATED_IPAGE_ALLOCATOR}"
        "${FACET_GENERATED_ILOGGER}"
        "${FACET_GENERATED_IDOMAIN_ENVIRONMENT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${FACET_GENERATED_INCLUDE}/facetos/interfaces"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IGENERIC}"
        "${FACETOS_ROOT}/idl/IGenericObject.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IDEBUG}"
        "${FACETOS_ROOT}/idl/IDebug.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_ILOGGING_CONFIG}"
        "${FACETOS_ROOT}/idl/ILoggingConfig.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_ILOGGING_SINK}"
        "${FACETOS_ROOT}/idl/ILoggingSink.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IDOMAIN_CONSOLE_CONFIG}"
        "${FACETOS_ROOT}/idl/IDomainConsoleConfig.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IDOMAIN_CONFIG}"
        "${FACETOS_ROOT}/idl/IDomainConfig.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IPAGE_ALLOCATOR}"
        "${FACETOS_ROOT}/idl/IPageAllocator.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_ILOGGER}"
        "${FACETOS_ROOT}/idl/ILogger.facet"
    COMMAND "${FACET_IDLC}" -o "${FACET_GENERATED_IDOMAIN_ENVIRONMENT}"
        "${FACETOS_ROOT}/idl/IDomainEnvironment.facet"
    DEPENDS
        "${FACET_IDLC}"
        "${FACETOS_ROOT}/idl/IGenericObject.facet"
        "${FACETOS_ROOT}/idl/IDebug.facet"
        "${FACETOS_ROOT}/idl/ILoggingConfig.facet"
        "${FACETOS_ROOT}/idl/ILoggingSink.facet"
        "${FACETOS_ROOT}/idl/IDomainConsoleConfig.facet"
        "${FACETOS_ROOT}/idl/IDomainConfig.facet"
        "${FACETOS_ROOT}/idl/IPageAllocator.facet"
        "${FACETOS_ROOT}/idl/ILogger.facet"
        "${FACETOS_ROOT}/idl/IDomainEnvironment.facet"
    VERBATIM
)

add_custom_target(facet-idl-debug-generated
    DEPENDS
        "${FACET_GENERATED_IGENERIC}"
        "${FACET_GENERATED_IDEBUG}"
)

add_custom_target(facet-idl-config-generated
    DEPENDS
        "${FACET_GENERATED_ILOGGING_CONFIG}"
        "${FACET_GENERATED_ILOGGING_SINK}"
        "${FACET_GENERATED_IDOMAIN_CONSOLE_CONFIG}"
        "${FACET_GENERATED_IDOMAIN_CONFIG}"
)

add_custom_target(facet-idl-page-allocator-generated
    DEPENDS
        "${FACET_GENERATED_IPAGE_ALLOCATOR}"
)

add_custom_target(facet-idl-environment-generated
    DEPENDS
        "${FACET_GENERATED_ILOGGER}"
        "${FACET_GENERATED_IDOMAIN_ENVIRONMENT}"
)

set(FACET_SHELL_CONTRACT_IDLS
    IPrincipal IHumanUser IPronouns IAuthenticatedPrincipal ISession
    ISecurityManager IAuthService IByteReader IByteWriter ITerminalControl
    ITerminal ISeat IProcessEnvironment IFileStore IFile IDirectory
    IProcessManager IProcess IProcessLifecycle
    IPOSIXView ICharacterStream IFileDescriptor IUnixMetadata
)
set(FACET_SHELL_CONTRACT_HEADERS)
foreach(FACET_SHELL_CONTRACT ${FACET_SHELL_CONTRACT_IDLS})
    set(FACET_SHELL_CONTRACT_HEADER
        "${FACET_GENERATED_INCLUDE}/facetos/interfaces/${FACET_SHELL_CONTRACT}.h")
    add_custom_command(
        OUTPUT "${FACET_SHELL_CONTRACT_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${FACET_GENERATED_INCLUDE}/facetos/interfaces"
        COMMAND "${FACET_IDLC}" -o "${FACET_SHELL_CONTRACT_HEADER}"
            "${FACETOS_ROOT}/idl/${FACET_SHELL_CONTRACT}.facet"
        DEPENDS "${FACET_IDLC}"
            "${FACETOS_ROOT}/idl/IGenericObject.facet"
            "${FACETOS_ROOT}/idl/${FACET_SHELL_CONTRACT}.facet"
        VERBATIM
    )
    list(APPEND FACET_SHELL_CONTRACT_HEADERS "${FACET_SHELL_CONTRACT_HEADER}")
endforeach()
add_custom_target(facet-idl-shell-contracts-generated
    DEPENDS ${FACET_SHELL_CONTRACT_HEADERS}
)


#
# FacetOS libfacet seL4 platform backend.
#

file(GLOB FACET_PLATFORM_SEL4_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/libfacet/src/platform/sel4/*.c"
)
list(REMOVE_ITEM FACET_PLATFORM_SEL4_SRCS
    "${FACETOS_ROOT}/libfacet/src/platform/sel4/client.c"
    "${FACETOS_ROOT}/libfacet/src/platform/sel4/service.c"
)

add_library(facet-platform-sel4 STATIC
    ${FACET_PLATFORM_SEL4_SRCS}
)

add_library(facet-platform-sel4-client STATIC
    "${FACETOS_ROOT}/libfacet/src/platform/sel4/client.c"
)

add_library(facet-platform-sel4-service STATIC
    "${FACETOS_ROOT}/libfacet/src/platform/sel4/service.c"
)

facetos_configure_target(facet-platform-sel4-service)
target_link_libraries(facet-platform-sel4-service PRIVATE
    facet_common sel4runtime sel4 muslc)

facetos_configure_target(facet-platform-sel4-client)

target_link_libraries(facet-platform-sel4-client PRIVATE
    sel4runtime
    sel4
    muslc
)

facetos_configure_target(facet-platform-sel4)

target_link_libraries(facet-platform-sel4 PRIVATE
    facet_common
    sel4runtime
    sel4
    utils
    sel4simple
    sel4simple-default
    sel4vka
    sel4vspace
    sel4utils
)


#
# FacetOS dominit child process.
#

file(GLOB FACET_DOMINIT_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/src/dominit/*.c"
    "${FACETOS_ROOT}/src/dominit/platform/*.c"
    "${FACETOS_ROOT}/src/dominit/platform/sel4/*.c"
)

add_executable(dominit
    ${FACET_DOMINIT_SRCS}
)

facetos_configure_target(dominit)
target_include_directories(dominit BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
)
add_dependencies(dominit facet-idl-debug-generated)
add_dependencies(dominit facet-idl-page-allocator-generated)
add_dependencies(dominit facet-idl-environment-generated)
add_dependencies(dominit facet-idl-shell-contracts-generated)

# `sel4` is CMake's libsel4 target. sel4runtime supplies process startup,
# TLS, and the IPC-buffer pointer; dominit supplies its own bump allocator,
# while muslc contributes the freestanding standard headers/runtime support.
target_link_libraries(dominit PRIVATE
    facet_common
    facet-platform-sel4-client
    sel4runtime
    sel4
    muslc
)

add_executable(FacetShell
    "${FACETOS_ROOT}/src/apps/facet_shell.c"
    "${FACETOS_ROOT}/src/apps/platform/allocator.c"
    "${FACETOS_ROOT}/src/apps/platform/liballoc.c"
    "${FACETOS_ROOT}/src/dominit/platform/sel4/api.c"
)
facetos_configure_target(FacetShell)
target_include_directories(FacetShell BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
)
add_dependencies(FacetShell facet-idl-page-allocator-generated)
add_dependencies(FacetShell facet-idl-environment-generated)
add_dependencies(FacetShell facet-idl-shell-contracts-generated)
target_link_libraries(FacetShell PRIVATE
    facet_common
    facet-platform-sel4-client
    sel4runtime
    sel4
    muslc
)

function(add_facet_native_utility target source)
    add_executable(${target}
        "${FACETOS_ROOT}/src/apps/${source}"
        "${FACETOS_ROOT}/src/apps/platform/allocator.c"
        "${FACETOS_ROOT}/src/apps/platform/liballoc.c"
        "${FACETOS_ROOT}/src/dominit/platform/sel4/api.c"
    )
    facetos_configure_target(${target})
    target_include_directories(${target} BEFORE PRIVATE
        "${FACET_GENERATED_INCLUDE}"
    )
    add_dependencies(${target} facet-idl-page-allocator-generated)
    add_dependencies(${target} facet-idl-environment-generated)
    add_dependencies(${target} facet-idl-shell-contracts-generated)
    target_link_libraries(${target} PRIVATE
        facet_common facet-platform-sel4-client sel4runtime sel4 muslc)
endfunction()

add_facet_native_utility(ls facet_ls.c)
add_facet_native_utility(cat facet_cat.c)

add_executable(FacetLogin
    "${FACETOS_ROOT}/src/apps/facet_login.c"
    "${FACETOS_ROOT}/src/apps/platform/allocator.c"
    "${FACETOS_ROOT}/src/apps/platform/liballoc.c"
    "${FACETOS_ROOT}/src/dominit/platform/sel4/api.c"
)
facetos_configure_target(FacetLogin)
target_include_directories(FacetLogin BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
)
add_dependencies(FacetLogin facet-idl-page-allocator-generated)
add_dependencies(FacetLogin facet-idl-environment-generated)
add_dependencies(FacetLogin facet-idl-shell-contracts-generated)
target_link_libraries(FacetLogin PRIVATE
    facet_common facet-platform-sel4-client sel4runtime sel4 muslc)

function(add_facet_seat_server target definition)
    add_executable(${target}
        "${FACETOS_ROOT}/src/seat/main.c"
        "${FACETOS_ROOT}/src/seat/pc_console.c"
        "${FACETOS_ROOT}/src/seat/pc_cursor.c"
        "${FACETOS_ROOT}/src/seat/platform/sel4.c"
        "${FACETOS_ROOT}/src/apps/platform/allocator.c"
        "${FACETOS_ROOT}/src/apps/platform/liballoc.c"
    )
    facetos_configure_target(${target})
    target_include_directories(${target} BEFORE PRIVATE
        "${FACET_GENERATED_INCLUDE}"
        "${FACETOS_ROOT}/src/seat"
        "${FACETOS_ROOT}/src/apps"
    )
    target_compile_definitions(${target} PRIVATE ${definition}=1)
    add_dependencies(${target} facet-idl-page-allocator-generated)
    add_dependencies(${target} facet-idl-shell-contracts-generated)
    target_link_libraries(${target} PRIVATE
        facet_common facet-platform-sel4-service sel4runtime sel4 muslc)
endfunction()

add_facet_seat_server(seat-server-serial FACET_SEAT_SERIAL)
add_facet_seat_server(seat-server-pc-console FACET_SEAT_PC)

function(add_facet_dummy target message)
    add_executable(${target}
        "${FACETOS_ROOT}/src/apps/facet_dummy.c"
        "${FACETOS_ROOT}/src/apps/platform/allocator.c"
        "${FACETOS_ROOT}/src/apps/platform/liballoc.c"
        "${FACETOS_ROOT}/src/dominit/platform/sel4/api.c"
    )
    facetos_configure_target(${target})
    target_include_directories(${target} BEFORE PRIVATE
        "${FACET_GENERATED_INCLUDE}"
    )
    add_dependencies(${target} facet-idl-environment-generated)
    add_dependencies(${target} facet-idl-page-allocator-generated)
    add_dependencies(${target} facet-idl-shell-contracts-generated)
    target_compile_definitions(${target} PRIVATE
        FACET_DUMMY_MESSAGE="${message}")
    target_link_libraries(${target} PRIVATE
        facet_common facet-platform-sel4-client sel4runtime sel4 muslc)
endfunction()

add_facet_dummy(FacetDummy "FacetDummy native override selected\\r\\n")
add_facet_dummy(dummysh "dummysh POSIX override selected\\r\\n")

add_subdirectory("${FACETOS_ROOT}/src/posix" facet-posix)


#
# FacetOS dominit0.
#
# Generic dominit0 sources plus the selected seL4 platform backend.
#

file(GLOB FACET_DOMINIT0_SRCS CONFIGURE_DEPENDS
    "${FACETOS_ROOT}/src/dominit0/*.c"
    "${FACETOS_ROOT}/src/dominit0/platform/sel4/*.c"
)

include(cpio)
MakeCPIO(dominit0_cpio.o "")

add_executable(dominit0
    ${FACET_DOMINIT0_SRCS}
    dominit0_cpio.o
)

facetos_configure_target(dominit0)
target_include_directories(dominit0 BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
)
add_dependencies(dominit0 facet-idl-debug-generated)
add_dependencies(dominit0 facet-idl-config-generated)
add_dependencies(dominit0 facet-idl-page-allocator-generated)
add_dependencies(dominit0 facet-idl-environment-generated)
add_dependencies(dominit0 facet-idl-shell-contracts-generated)

target_compile_definitions(dominit0 PRIVATE
    FACET_PLATFORM_SEL4=1
)


#
# Let upstream CMake targets carry their transitive include paths, generated
# configuration headers, compile definitions, and library dependencies.
#

target_link_libraries(dominit0 PRIVATE
    facet_klibc
    facet-platform-sel4
    facet_common
    facet_config

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
