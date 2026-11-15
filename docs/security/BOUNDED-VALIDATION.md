# ThemeLoader4: bounded security validation

Status: normative. Owner: security review. Replaces the S1 and S2 fuzz campaigns (owner decision of 2026-11-15).
This page is the production condition for the ThemeLoader4 parsers. It is a check with an objective, a budget and
a stop rule. It is not a campaign, and run counts are not a result.

## 0. Priority (owner decision of 2026-11-15)

Functionality first, essential safety throughout, exhaustive hardening after the architecture has shown that it
works. The first goal is an end-to-end demonstration: load a complete version-4 style, resolve its resources, draw
real themed controls in an isolated environment, and compare the result with Windows. No bounded run is required
before that demonstration, and none delays it. What stays on every commit is the essential part: bounds checks in
the code, the unit tests with the sanitizers, and the review of the diff. A release candidate, and so the check of
this page, exists only after the demonstration. Large-scale coverage-guided fuzzing is resumed by an owner decision
once the implementation is demonstrated and its interfaces are stable; it is not part of this page.

## 1. What must be true for a release candidate

A release candidate is one commit of ThemeLoader4 `main`. It passes when all of these hold on that commit:

1. The unit tests pass with gcc and with clang ASan+UBSan, 0 warnings (already run on every commit).
2. The corpus test accepts the 32 reference styles and the shipped version-3 style test passes (run where the
   inputs are; they are never committed).
3. Each parser below passes its bounded run.
4. The security reviewer has read the diff since the last candidate that passed.

## 2. The bounded run, per parser

| Parser | Harness | Objective: no input makes the parser... |
|---|---|---|
| PE and resource reader | `fuzz_pe` | read outside the mapping, loop without bound, or return a resource range outside the file |
| Version-4 container and model | `fuzz_v4_style`, `fuzz_v4_blob`, `fuzz_v4_model`, `fuzz_v4_two_pass` | accept a container whose model then reads outside its buffer; give a different answer when the bytes change between the two passes |
| PNG chunk walker | `fuzz_png` | pass an image to a decoder that breaks the limits of the requirements |
| String tables | `fuzz_strings` | return a string range outside its block, or write past the caller's buffer |
| Security descriptor test | `fuzz_trust_sd` | call a descriptor "not user-writable" when an entry grants a write right, or read outside the descriptor |

Each harness already states these as promises that abort. The DirectUI parsers get a row when they get a consumer.

**Budget.** At most 15 minutes on 4 CPUs per parser row, on the ASan+UBSan build, started from the generated seeds.
Five rows: at most 75 minutes per release candidate. No run outside a release candidate.

**Preflight (owner rule of 2026-11-15).** Before the budget starts: every harness binary exists, its seed directory
is not empty, and a 3-second run of each ends normally. A failed preflight stops the whole check with a message
that names the harness. No budget is spent on a setup that was not shown to work.

**Stop rule.** A row stops at the first of: any finding; no new coverage for 5 minutes; the 15 minutes.

**A finding** is a crash, a sanitizer report, a broken promise, a timeout or an out-of-memory stop. It triggers, in
this order: the candidate fails; the input is reduced and kept outside the repository if it holds third-party
bytes, inside as a generated regression test otherwise; a task is written with owner, file and line, and the
reproducer command; the fix is reviewed; the same row runs again with the same budget on the fixed commit.

**Report.** One line per row: commit, harness, passed or failed, which stop rule ended it, finding ids. Nothing else.

## 3. What this does not claim

- A passed row means "no finding inside this budget". It is not a proof.
- The MemorySanitizer build is not part of the condition. It needs the container's system-call filter relaxed
  (measured on libernt-srv: 4 of 6 harnesses failed to start at random with the default filter) and has never run.
- Input from real third-party styles is covered by the corpus test of 1.2, not by these runs.

## 4. What the stopped S1 campaign showed

Stopped 2026-11-15 14:38Z on the owner's order; container and directory removed from libernt-srv. It ran the
DirectUI harnesses `sheet`, `two_pass` and `xml` (xml stopped in its fourth round) on the ASan+UBSan build of an
earlier commit: no finding. `value`, `layout` and `pe` did not run in the campaign, and the MemorySanitizer build
did not run at all. No S1 report will be written.

## 5. Where this is required

`REQUIREMENTS.md` R-PROC-3 and R-DUI-18, and `THEME-TRUST-POLICY.md` T-GATE-1, make this check the condition for
production use. `S1-FUZZ-PLAN.md` keeps only the promises of the harnesses (its sections 3 and 4).
