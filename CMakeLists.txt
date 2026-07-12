cmake_minimum_required(VERSION 3.12)
project(protosched VERSION 1.0.0 LANGUAGES C)

add_library(protosched STATIC protosched.c)
set_target_properties(protosched PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED ON
)
target_include_directories(protosched PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(protosched PRIVATE
    -pedantic
    -Wconversion -Wshadow
    -Wall -Wextra -Werror
    -Wno-implicit-fallthrough
)    

option(USE_GNU_EXTENSIONS "Enable GNU extensions" ON)
if(USE_GNU_EXTENSIONS)
    set_target_properties(protosched PROPERTIES C_EXTENSIONS ON)
    message(STATUS "protosched: GNU extensions mode")
else()
    set_target_properties(protosched PROPERTIES C_EXTENSIONS OFF)
    target_compile_definitions(protosched PRIVATE PS_STRICT_C89=1)
    message(STATUS "protosched: strict C89 mode")
endif()

option(BUILD_EXAMPLES "Build protosched examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(BUILD_TESTS "Build protosched unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
