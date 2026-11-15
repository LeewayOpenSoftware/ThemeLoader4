# DirectUI component: UIFILE parsing and the element model.
# Independent of loader/: this directory never links or includes it.

add_library(tl4_directui_core STATIC
    src/dui_version.c
    src/dui_xml.c
    src/dui_value.c
    src/dui_sheet.c
    src/dui_theme.c
    src/dui_layout.c
)
target_include_directories(tl4_directui_core PUBLIC include)
tl4_warnings(tl4_directui_core)

if(TL4_BUILD_TESTS)
    tl4_add_test(tl4_directui_smoke SOURCES tests/test_smoke.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_xml SOURCES tests/test_xml.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_value SOURCES tests/test_value.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_sheet SOURCES tests/test_sheet.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_layout SOURCES tests/test_layout.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_corpus_xml SOURCES tests/corpus_xml.c LIBS tl4_directui_core)
    tl4_add_test(tl4_directui_corpus_sheet SOURCES tests/corpus_sheet.c LIBS tl4_directui_core)
endif()

tl4_add_fuzzer(fuzz_xml SOURCES fuzz/fuzz_xml.c LIBS tl4_directui_core)
tl4_add_fuzzer(fuzz_value SOURCES fuzz/fuzz_value.c LIBS tl4_directui_core)
tl4_add_fuzzer(fuzz_sheet SOURCES fuzz/fuzz_sheet.c LIBS tl4_directui_core)
tl4_add_fuzzer(fuzz_layout SOURCES fuzz/fuzz_layout.c LIBS tl4_directui_core)
