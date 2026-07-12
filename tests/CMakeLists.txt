file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "test_*.c")

foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_compile_options(${TEST_NAME} PRIVATE -Wno-implicit-fallthrough)
    target_link_libraries(${TEST_NAME} PRIVATE protosched)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()