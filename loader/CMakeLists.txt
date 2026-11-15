# Theme Loader 4: the version-4 style loader. Independent of directui/: this directory never links or includes it.
# docs/security/REQUIREMENTS.md is normative for everything here.

add_library(tl4_loader_core STATIC
    src/pe.c
)
target_include_directories(tl4_loader_core PUBLIC include)
tl4_warnings(tl4_loader_core)

if(TL4_BUILD_TESTS)
    # Fixtures: tiny PE images with a resource section, built in memory. Every test input is generated (R-PROC-1).
    add_library(tl4_loader_fixtures STATIC tests/pebuild.c)
    target_include_directories(tl4_loader_fixtures PUBLIC tests)
    tl4_warnings(tl4_loader_fixtures)

    tl4_add_test(tl4_loader_test_pe SOURCES tests/test_pe.c LIBS tl4_loader_core tl4_loader_fixtures)

    # Seeds for the fuzzers, written where the CI smoke run looks for them: <build>/loader/fuzz/seeds/<harness>
    add_executable(tl4_loader_mkseeds fuzz/mkseeds.c)
    target_link_libraries(tl4_loader_mkseeds PRIVATE tl4_loader_fixtures)
    tl4_warnings(tl4_loader_mkseeds)
    if(MSVC)
        target_compile_definitions(tl4_loader_mkseeds PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    set(TL4_LOADER_SEEDS "${CMAKE_CURRENT_BINARY_DIR}/fuzz/seeds/pe")
    add_test(NAME tl4_loader_seeds_dir COMMAND ${CMAKE_COMMAND} -E make_directory "${TL4_LOADER_SEEDS}")
    add_test(NAME tl4_loader_mkseeds COMMAND tl4_loader_mkseeds "${TL4_LOADER_SEEDS}")
    set_tests_properties(tl4_loader_mkseeds PROPERTIES DEPENDS tl4_loader_seeds_dir)

    if(NOT TL4_FUZZ)
        # the harness as a plain program: every generated seed goes through LLVMFuzzerTestOneInput once
        add_executable(tl4_loader_fuzz_pe_seeds fuzz/fuzz_pe.c)
        target_compile_definitions(tl4_loader_fuzz_pe_seeds PRIVATE TL4_FUZZ_STANDALONE=1)
        if(MSVC)
            target_compile_definitions(tl4_loader_fuzz_pe_seeds PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        target_link_libraries(tl4_loader_fuzz_pe_seeds PRIVATE tl4_loader_core)
        tl4_warnings(tl4_loader_fuzz_pe_seeds)
        add_test(NAME tl4_loader_fuzz_pe_seeds COMMAND tl4_loader_fuzz_pe_seeds
                 "${TL4_LOADER_SEEDS}/style_pe32plus.bin" "${TL4_LOADER_SEEDS}/style_pe32.bin"
                 "${TL4_LOADER_SEEDS}/style_sections.bin" "${TL4_LOADER_SEEDS}/v3_like.bin" "${TL4_LOADER_SEEDS}/one_resource.bin")
        set_tests_properties(tl4_loader_fuzz_pe_seeds PROPERTIES DEPENDS tl4_loader_mkseeds)
    endif()
endif()

# R-PE-11: the libFuzzer harness lives in the same commit as the reader; the CI smoke run feeds it the seeds above.
tl4_add_fuzzer(fuzz_pe SOURCES fuzz/fuzz_pe.c LIBS tl4_loader_core)
