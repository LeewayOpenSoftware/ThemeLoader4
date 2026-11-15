# loader/: Theme Loader 4

Core in C11, host-portable. It has no Windows API dependency, so it can be fuzzed on Linux. Platform glue (file mapping, GDI+ hand-off) lives in a separate thin layer.

Order of work: see milestones L0–L3 in the top-level README. Before writing any reader code, read `docs/security/REQUIREMENTS.md`.
