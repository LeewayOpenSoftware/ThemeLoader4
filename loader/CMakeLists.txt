# Theme Loader 4: the version-4 style loader. Independent of directui/: this directory never links or includes it.
# docs/security/REQUIREMENTS.md is normative for everything here.

add_library(tl4_loader_core STATIC
    src/pe.c
    src/v4.c
    src/model.c
    src/png.c
)
target_include_directories(tl4_loader_core PUBLIC include)
tl4_warnings(tl4_loader_core)

# The same sources with TL4_TESTING: adds the between-passes hook the R-GEN-11 tests of the model need (R-MOD-5).
if(TL4_BUILD_TESTS)
    add_library(tl4_loader_core_testing STATIC src/pe.c src/v4.c src/model.c src/png.c)
    target_include_directories(tl4_loader_core_testing PUBLIC include)
    target_compile_definitions(tl4_loader_core_testing PUBLIC TL4_TESTING)
    tl4_warnings(tl4_loader_core_testing)
endif()

if(TL4_BUILD_TESTS)
    # Fixtures: tiny PE images with a resource section and version-4 resources, built in memory. Every test input is
    # generated (R-PROC-1).
    add_library(tl4_loader_fixtures STATIC tests/pebuild.c tests/v4build.c tests/v4fixture.c tests/pngbuild.c)
    target_include_directories(tl4_loader_fixtures PUBLIC tests)
    tl4_warnings(tl4_loader_fixtures)

    tl4_add_test(tl4_loader_test_pe SOURCES tests/test_pe.c LIBS tl4_loader_core tl4_loader_fixtures)
    tl4_add_test(tl4_loader_test_v4 SOURCES tests/test_v4.c LIBS tl4_loader_core tl4_loader_fixtures)
    tl4_add_test(tl4_loader_test_model SOURCES tests/test_model.c LIBS tl4_loader_core_testing tl4_loader_fixtures)
    tl4_add_test(tl4_loader_test_png SOURCES tests/test_png.c LIBS tl4_loader_core tl4_loader_fixtures)
    # R-V4-14: a style at the limits validates inside the fuzz timeout; the CPU bound applies to plain builds only
    tl4_add_test(tl4_loader_test_v4_limits SOURCES tests/test_v4_limits.c LIBS tl4_loader_core_testing tl4_loader_fixtures)
    if(TL4_SANITIZE OR TL4_FUZZ)
        target_compile_definitions(tl4_loader_test_v4_limits PRIVATE TL4_SANITIZED=1)
        target_compile_definitions(tl4_loader_test_png PRIVATE TL4_SANITIZED=1)
    endif()

    # Corpus runner (R-V4-13): reads TL4_CORPUS_DIR at run time, exit 77 = skipped when it is unset. The reference
    # files never enter the repository; tests/corpus-expected.csv holds their statistics only.
    # R-PE-8 (2026-11-15): the shipped version-3 style, read from TL4_SHIPPED_STYLE at run time; exit 77 = skipped.
    # The file never enters the repository (R-PROC-1).
    tl4_add_test(tl4_loader_test_shipped SOURCES tests/test_shipped.c LIBS tl4_loader_core)
    if(MSVC)
        target_compile_definitions(tl4_loader_test_shipped PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    tl4_add_test(tl4_loader_corpus_v4 SOURCES tests/corpus_v4.c LIBS tl4_loader_core)
    set_tests_properties(tl4_loader_corpus_v4 PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        ENVIRONMENT "TL4_CORPUS_DIR=$ENV{TL4_CORPUS_DIR}")
    if(MSVC)
        target_compile_definitions(tl4_loader_corpus_v4 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    # Seeds for the fuzzers, written where the CI smoke run looks for them: <build>/loader/fuzz/seeds/<harness>
    # The query list for the Windows reference probe (tools/vsprobe): class names and ids of a style's model, no values
    add_executable(tl4_loader_querylist tools/querylist.c)
    target_link_libraries(tl4_loader_querylist PRIVATE tl4_loader_core)
    tl4_warnings(tl4_loader_querylist)
    if(MSVC)
        target_compile_definitions(tl4_loader_querylist PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    add_executable(tl4_loader_mkseeds fuzz/mkseeds.c)
    target_link_libraries(tl4_loader_mkseeds PRIVATE tl4_loader_fixtures)
    tl4_warnings(tl4_loader_mkseeds)
    if(MSVC)
        target_compile_definitions(tl4_loader_mkseeds PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    set(TL4_LOADER_SEEDS "${CMAKE_CURRENT_BINARY_DIR}/fuzz/seeds")
    add_test(NAME tl4_loader_seeds_dir COMMAND ${CMAKE_COMMAND} -E make_directory
             "${TL4_LOADER_SEEDS}/pe" "${TL4_LOADER_SEEDS}/v4_style" "${TL4_LOADER_SEEDS}/v4_blob" "${TL4_LOADER_SEEDS}/v4_model" "${TL4_LOADER_SEEDS}/v4_two_pass" "${TL4_LOADER_SEEDS}/png")
    add_test(NAME tl4_loader_mkseeds COMMAND tl4_loader_mkseeds "${TL4_LOADER_SEEDS}")
    set_tests_properties(tl4_loader_mkseeds PROPERTIES DEPENDS tl4_loader_seeds_dir)

    if(NOT TL4_FUZZ)
        # the harnesses as plain programs: every generated seed goes through LLVMFuzzerTestOneInput once
        foreach(h pe v4_style v4_blob v4_model png)
            add_executable(tl4_loader_fuzz_${h}_seeds fuzz/fuzz_${h}.c)
            target_compile_definitions(tl4_loader_fuzz_${h}_seeds PRIVATE TL4_FUZZ_STANDALONE=1)
            if(MSVC)
                target_compile_definitions(tl4_loader_fuzz_${h}_seeds PRIVATE _CRT_SECURE_NO_WARNINGS)
            endif()
            target_link_libraries(tl4_loader_fuzz_${h}_seeds PRIVATE tl4_loader_core)
            tl4_warnings(tl4_loader_fuzz_${h}_seeds)
        endforeach()
        add_test(NAME tl4_loader_fuzz_pe_seeds COMMAND tl4_loader_fuzz_pe_seeds
                 "${TL4_LOADER_SEEDS}/pe/style_pe32plus.bin" "${TL4_LOADER_SEEDS}/pe/style_pe32.bin"
                 "${TL4_LOADER_SEEDS}/pe/style_sections.bin" "${TL4_LOADER_SEEDS}/pe/v3_like.bin" "${TL4_LOADER_SEEDS}/pe/one_resource.bin"
                 "${TL4_LOADER_SEEDS}/pe/v3_languages.bin" "${TL4_LOADER_SEEDS}/pe/style_languages.bin")
        add_test(NAME tl4_loader_fuzz_v4_style_seeds COMMAND tl4_loader_fuzz_v4_style_seeds
                 "${TL4_LOADER_SEEDS}/v4_style/valid.bin" "${TL4_LOADER_SEEDS}/v4_style/no_bcmap.bin" "${TL4_LOADER_SEEDS}/v4_style/empty_variant.bin")
        add_test(NAME tl4_loader_fuzz_v4_blob_seeds COMMAND tl4_loader_fuzz_v4_blob_seeds
                 "${TL4_LOADER_SEEDS}/v4_blob/cmap.bin" "${TL4_LOADER_SEEDS}/v4_blob/vmap.bin" "${TL4_LOADER_SEEDS}/v4_blob/bcmap.bin"
                 "${TL4_LOADER_SEEDS}/v4_blob/variant.bin" "${TL4_LOADER_SEEDS}/v4_blob/rmap.bin"
                 "${TL4_LOADER_SEEDS}/v4_blob/strings.bin")
        add_test(NAME tl4_loader_fuzz_v4_model_seeds COMMAND tl4_loader_fuzz_v4_model_seeds
                 "${TL4_LOADER_SEEDS}/v4_model/valid.bin" "${TL4_LOADER_SEEDS}/v4_model/no_bcmap.bin" "${TL4_LOADER_SEEDS}/v4_model/empty_variant.bin"
                 "${TL4_LOADER_SEEDS}/v4_model/duplicates.bin")
        add_test(NAME tl4_loader_fuzz_png_seeds COMMAND tl4_loader_fuzz_png_seeds
                 "${TL4_LOADER_SEEDS}/png/rgba_text.bin" "${TL4_LOADER_SEEDS}/png/rgb_srgb.bin" "${TL4_LOADER_SEEDS}/png/rgba_srgb_text.bin"
                 "${TL4_LOADER_SEEDS}/png/minimal.bin")
        # the two-pass harness needs the hook, which only the testing build of the core has
        add_executable(tl4_loader_fuzz_v4_two_pass_seeds fuzz/fuzz_v4_two_pass.c)
        target_compile_definitions(tl4_loader_fuzz_v4_two_pass_seeds PRIVATE TL4_FUZZ_STANDALONE=1)
        if(MSVC)
            target_compile_definitions(tl4_loader_fuzz_v4_two_pass_seeds PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        target_link_libraries(tl4_loader_fuzz_v4_two_pass_seeds PRIVATE tl4_loader_core_testing)
        tl4_warnings(tl4_loader_fuzz_v4_two_pass_seeds)
        add_test(NAME tl4_loader_fuzz_v4_two_pass_seeds COMMAND tl4_loader_fuzz_v4_two_pass_seeds
                 "${TL4_LOADER_SEEDS}/v4_two_pass/valid_valid.bin" "${TL4_LOADER_SEEDS}/v4_two_pass/valid_dups.bin"
                 "${TL4_LOADER_SEEDS}/v4_two_pass/valid_lang.bin")
        set_tests_properties(tl4_loader_fuzz_pe_seeds tl4_loader_fuzz_v4_style_seeds tl4_loader_fuzz_v4_blob_seeds tl4_loader_fuzz_v4_model_seeds tl4_loader_fuzz_v4_two_pass_seeds tl4_loader_fuzz_png_seeds
                             PROPERTIES DEPENDS tl4_loader_mkseeds)
    endif()
endif()

# R-PE-11 / R-V4: the libFuzzer harnesses live in the same commits as the code they test; the CI smoke run feeds them
# the seeds above.
tl4_add_fuzzer(fuzz_pe SOURCES fuzz/fuzz_pe.c LIBS tl4_loader_core)
tl4_add_fuzzer(fuzz_v4_style SOURCES fuzz/fuzz_v4_style.c LIBS tl4_loader_core)
tl4_add_fuzzer(fuzz_v4_blob SOURCES fuzz/fuzz_v4_blob.c LIBS tl4_loader_core)
tl4_add_fuzzer(fuzz_v4_model SOURCES fuzz/fuzz_v4_model.c LIBS tl4_loader_core)
tl4_add_fuzzer(fuzz_png SOURCES fuzz/fuzz_png.c LIBS tl4_loader_core)
if(TARGET tl4_loader_core_testing)
    # R-GEN-11 inside the build: needs the between-passes hook of the testing core
    tl4_add_fuzzer(fuzz_v4_two_pass SOURCES fuzz/fuzz_v4_two_pass.c LIBS tl4_loader_core_testing)
endif()
