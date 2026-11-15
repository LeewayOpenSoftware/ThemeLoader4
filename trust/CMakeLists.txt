# Trust decisions of the I1 integration (THEME-TRUST-POLICY T-DEF-1..5): pure functions, no OS call.
# Independent of loader/ and directui/; LiberNT uxtheme vendors it next to the loader core.

add_library(tl4_trust STATIC src/trust.c)
target_include_directories(tl4_trust PUBLIC include)
tl4_warnings(tl4_trust)

if(TL4_BUILD_TESTS)
    tl4_add_test(tl4_trust_test SOURCES tests/test_trust.c LIBS tl4_trust)
endif()
