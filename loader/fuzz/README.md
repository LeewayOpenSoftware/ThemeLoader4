# Fuzz harnesses

Every reader in `loader/` gets a libFuzzer harness from its first commit (REQUIREMENTS A.14). The harnesses live
here and are built two ways:

- `-DTL4_FUZZ=ON` with clang: real libFuzzer targets (`fuzz_pe`, ...) with AddressSanitizer and
  UndefinedBehaviorSanitizer (the top-level build adds the sanitizers). The CI smoke step runs each one for 20,000
  inputs on the generated seeds in `<build>/loader/fuzz/seeds/<harness>`.
- otherwise: the same source as a standalone program (`tl4_loader_fuzz_pe_seeds`) that feeds the generated seeds
  to `LLVMFuzzerTestOneInput` under ctest, so gcc and MSVC builds still exercise the harness.

Seeds are generated at test time by `mkseeds` from `loader/tests/pebuild.c` into `<build>/loader/fuzz/seeds/pe/`. No seed
is derived from a Microsoft or third-party file; the corpus for compatibility tests is given by path at run time
(`loader/tests/CORPUS.md`) and never enters the fuzz inputs committed to this repository.

Running a campaign (Linux, clang):

```
cmake -S . -B out/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DTL4_FUZZ=ON
cmake --build out/fuzz && ctest --test-dir out/fuzz
mkdir -p out/fuzz-corpus-pe && out/fuzz/loader/fuzz_pe -max_len=4194304 -jobs=8 -workers=8 out/fuzz-corpus-pe out/fuzz/loader/fuzz/seeds/pe
```

Crashes (`crash-*`, `leak-*`, `timeout-*` files) are reproduced with `fuzz_pe <file>` and are the input of the S1
review. The fuzzer's own corpus directory stays outside the repository.
