# Theme Loader 4: top-level build. loader/ and directui/ are separate targets;
# neither links the other.
cmake_minimum_required(VERSION 3.20)
project(TL4 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TL4_BUILD_LOADER "Build the version-4 style loader" ON)
option(TL4_BUILD_DIRECTUI "Build the DirectUI component" ON)
option(TL4_BUILD_TESTS "Build the unit tests and corpus runners" ON)
option(TL4_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TL4_FUZZ "Build the libFuzzer harnesses (Clang only)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TL4Build)

if(TL4_SANITIZE OR TL4_FUZZ)
    if(MSVC)
        message(FATAL_ERROR "TL4_SANITIZE and TL4_FUZZ need GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(TL4_FUZZ AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TL4_FUZZ needs Clang (libFuzzer)")
endif()
if(TL4_FUZZ)
    # Coverage instrumentation for every object, not only the harness: without it libFuzzer sees
    # the edges of the harness file alone and mutates blind inside the parsers.
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

if(TL4_BUILD_TESTS)
    enable_testing()
endif()

if(TL4_BUILD_LOADER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/loader/CMakeLists.txt")
    add_subdirectory(loader)
endif()

if(TL4_BUILD_DIRECTUI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/directui/CMakeLists.txt")
    add_subdirectory(directui)
endif()
