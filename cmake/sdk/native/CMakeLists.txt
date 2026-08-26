set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -Wl,--start-group ${libgcc} <LINK_LIBRARIES> -Wl,--end-group -o <TARGET>")

set(FACET_NATIVE_CRT
    "${FACETOS_ROOT}/src/libc/native/start.S"
    "${FACETOS_ROOT}/src/libc/native/start.c"
)

function(configure_facet_native target)
    facetos_configure_target(${target})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_include_directories(${target} BEFORE PRIVATE
        "${FACET_GENERATED_INCLUDE}")
    add_dependencies(${target} facet-idl-page-allocator-generated)
    add_dependencies(${target} facet-idl-environment-generated)
    add_dependencies(${target} facet-idl-shell-contracts-generated)
endfunction()

function(add_facet_native_program target source)
    add_executable(${target} ${FACET_NATIVE_CRT}
        "${FACETOS_ROOT}/src/apps/${source}")
    configure_facet_native(${target})
    target_link_libraries(${target} PRIVATE facet_common
        facet-app-platform-sel4 libc-native)
endfunction()

add_facet_native_program(FacetShell facet_shell.c)
add_facet_native_program(FacetLogin facet_login.c)
add_facet_native_program(ls facet_ls.c)
add_facet_native_program(cat facet_cat.c)

function(add_facet_seat_server target definition)
    add_executable(${target} ${FACET_NATIVE_CRT}
        "${FACETOS_ROOT}/src/seat/main.c"
        "${FACETOS_ROOT}/src/seat/pc_console.c"
        "${FACETOS_ROOT}/src/seat/pc_cursor.c"
        "${FACETOS_ROOT}/src/seat/platform/sel4.c")
    configure_facet_native(${target})
    target_include_directories(${target} BEFORE PRIVATE
        "${FACETOS_ROOT}/src/seat" "${FACETOS_ROOT}/src/apps")
    target_compile_definitions(${target} PRIVATE ${definition}=1)
    target_link_libraries(${target} PRIVATE facet_common
        facet-platform-sel4-service libc-native sel4runtime sel4)
endfunction()

add_facet_seat_server(seat-server-serial FACET_SEAT_SERIAL)
add_facet_seat_server(seat-server-pc-console FACET_SEAT_PC)

function(add_facet_dummy target message)
    add_facet_native_program(${target} facet_dummy.c)
    target_compile_definitions(${target} PRIVATE FACET_DUMMY_MESSAGE="${message}")
endfunction()

add_facet_dummy(FacetDummy "FacetDummy native override selected\\r\\n")
add_facet_dummy(dummysh "dummysh POSIX override selected\\r\\n")
