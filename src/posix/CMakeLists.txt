# POSIX programs use their own SysV entry point.  Override the musllibc link
# template in this directory so the global seL4 crt0/crti/crtn objects are not
# inserted ahead of our startup code.
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -Wl,--start-group ${libgcc} <LINK_LIBRARIES> -Wl,--end-group -o <TARGET>")

add_executable(PosixLogin
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.S"
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.c"
    "${FACETOS_ROOT}/src/apps/posix/login.c"
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
    facet_common facet-app-platform-sel4 libc-posix)
target_link_options(PosixLogin PRIVATE
    "-Wl,-Map,${CMAKE_BINARY_DIR}/PosixLogin.map")

add_executable(PosixTestPerms
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.S"
    "${CMAKE_CURRENT_LIST_DIR}/crt/start.c"
    "${FACETOS_ROOT}/src/apps/posix/test_perms.c"
)
facetos_configure_target(PosixTestPerms)
target_include_directories(PosixTestPerms BEFORE PRIVATE
    "${FACET_GENERATED_INCLUDE}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_include_directories(PosixTestPerms SYSTEM PRIVATE
    "${CMAKE_BINARY_DIR}/musllibc/build-temp/stage/include"
)
add_dependencies(PosixTestPerms facet-idl-shell-contracts-generated)
target_link_libraries(PosixTestPerms PRIVATE
    facet_common facet-app-platform-sel4 libc-posix)
target_link_options(PosixTestPerms PRIVATE
    "-Wl,-Map,${CMAKE_BINARY_DIR}/PosixTestPerms.map")

add_executable(PosixInit "${CMAKE_CURRENT_LIST_DIR}/crt/start.S" "${CMAKE_CURRENT_LIST_DIR}/crt/start.c" "${FACETOS_ROOT}/src/apps/posix/init.c")
add_executable(PosixSh "${CMAKE_CURRENT_LIST_DIR}/crt/start.S" "${CMAKE_CURRENT_LIST_DIR}/crt/start.c" "${FACETOS_ROOT}/src/apps/posix/sh.c")
add_executable(PosixLs "${CMAKE_CURRENT_LIST_DIR}/crt/start.S" "${CMAKE_CURRENT_LIST_DIR}/crt/start.c" "${FACETOS_ROOT}/src/apps/posix/ls.c")
add_executable(PosixCat "${CMAKE_CURRENT_LIST_DIR}/crt/start.S" "${CMAKE_CURRENT_LIST_DIR}/crt/start.c" "${FACETOS_ROOT}/src/apps/posix/cat.c")
foreach(program PosixInit PosixSh PosixLs PosixCat)
    facetos_configure_target(${program})
    target_include_directories(${program} BEFORE PRIVATE "${FACET_GENERATED_INCLUDE}" "${CMAKE_CURRENT_LIST_DIR}/include")
    target_include_directories(${program} SYSTEM PRIVATE "${CMAKE_BINARY_DIR}/musllibc/build-temp/stage/include")
    add_dependencies(${program} facet-idl-shell-contracts-generated)
    target_link_libraries(${program} PRIVATE facet_common facet-app-platform-sel4 libc-posix)
endforeach()
