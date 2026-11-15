# DirectUI component: UIFILE parsing and the element model.
# Independent of loader/: this directory never links or includes it.

add_library(tl4_directui_core STATIC
    src/dui_version.c
)
target_include_directories(tl4_directui_core PUBLIC include)
tl4_warnings(tl4_directui_core)

if(TL4_BUILD_TESTS)
    tl4_add_test(tl4_directui_smoke SOURCES tests/test_smoke.c LIBS tl4_directui_core)
endif()
