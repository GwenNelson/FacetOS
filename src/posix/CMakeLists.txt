# POSIX programs use their own SysV entry point.  Override the musllibc link
# template in this directory so the global seL4 crt0/crti/crtn objects are not
# inserted ahead of our startup code.
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -Wl,--start-group ${libgcc} <LINK_LIBRARIES> -Wl,--end-group -o <TARGET>")

add_executable(PosixLogin
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.S"
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.c"
    "${CMAKE_CURRENT_LIST_DIR}/bin/login/main.c"
)
facetos_configure_target(PosixLogin)
target_include_directories(PosixLogin BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_include_directories(PosixLogin SYSTEM PRIVATE
    "${CMAKE_BINARY_DIR}/musllibc/build-temp/stage/include"
)
add_dependencies(PosixLogin facet-idl-shell-contracts-generated)
target_link_libraries(PosixLogin PRIVATE
    facet_common facet-platform-sel4-client libc-posix sel4runtime sel4)
