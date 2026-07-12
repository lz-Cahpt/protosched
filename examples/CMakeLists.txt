file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS "example_*.c")

foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_compile_options(${EXAMPLE_NAME} PRIVATE -Wno-implicit-fallthrough)
    target_link_libraries(${EXAMPLE_NAME} PRIVATE protosched)
    add_test(NAME ${EXAMPLE_NAME} COMMAND ${EXAMPLE_NAME})
endforeach()