# Shared build helpers for every TL4 component.

# R-GEN-9: warning-free with -Wall -Wextra -Wconversion (/W4 on MSVC), as errors.
function(tl4_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes -Werror)
    endif()
endfunction()

# A plain C test executable registered with CTest. Exit code 77 means "skipped"
# (corpus runners without TL4_CORPUS_DIR).
function(tl4_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE ${T_LIBS})
    tl4_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

# A libFuzzer harness, built only with TL4_FUZZ.
function(tl4_add_fuzzer name)
    cmake_parse_arguments(F "" "" "SOURCES;LIBS" ${ARGN})
    if(NOT TL4_FUZZ)
        return()
    endif()
    add_executable(${name} ${F_SOURCES})
    target_link_libraries(${name} PRIVATE ${F_LIBS})
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    tl4_warnings(${name})
endfunction()
