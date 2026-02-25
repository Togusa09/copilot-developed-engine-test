# Future Work: C++ Guardrails for LLM-Generated Code

Last updated: 2026-02-26

## Goal
Reduce common Copilot/LLM mistakes in this C++ engine by enforcing compile-time, static-analysis, runtime-safety, and CI quality gates.

## Current Context Snapshot
- Repository: EngineTest (C++20, CMake)
- Main targets:
  - `Engine` (static library)
  - `Sandbox` (executable)
- Existing build paths/patterns in workspace:
  - `build-vs` (Visual Studio generator)
  - `build-ninja` (Ninja generator)
- Existing VS Code tasks include configure/build/run for VS2026 and Ninja.

## Problem Areas to Prevent

### 1) Coding mistakes (LLM tendency)
- Hallucinated or mismatched APIs/signatures.
- Non-minimal diffs and accidental broad refactors.
- Build wiring omissions (include dirs, target links, CMake updates).
- Platform assumptions leaking into public interfaces.
- Exception-heavy control flow where explicit status returns are expected.

### 2) Security mistakes
- Missing input validation and bounds checks.
- Command/path injection patterns when constructing shell strings.
- Integer overflow/underflow in size/index arithmetic.
- Lifetime issues in async code (captured references outlive owners).
- Sensitive data leakage in logs/diagnostics.

### 3) Memory/lifetime mistakes
- Raw owning pointers (`new/delete`) and non-RAII cleanup paths.
- Ambiguous ownership across interfaces.
- Returning `string_view`/`span` to short-lived storage.
- Hot-loop heap allocations and vector invalidation hazards.
- Use-after-free/double-free/UB due to weak lifetime boundaries.

## Recommended Configuration Updates

### A) Compiler warnings and hardening
Apply strict warnings to all engine/app targets and fail on warnings in CI.

- MSVC:
  - `/W4`
  - `/WX` (at least in CI)
  - `/permissive-`
  - `/sdl`
  - `/guard:cf` (where applicable)
- Clang/GCC (for cross-check jobs):
  - `-Wall -Wextra -Wpedantic`
  - `-Werror` (at least in CI)
  - `-Wconversion -Wsign-conversion`

### B) Static analysis
- Add `clang-tidy` execution as a CI gate.
- Add `cppcheck` as a secondary pass.
- Add MSVC `/analyze` in a dedicated Windows analysis job.

### C) Runtime sanitizers
- Add dedicated sanitizer presets/jobs:
  - AddressSanitizer (ASan) debug configuration.
  - UndefinedBehaviorSanitizer (UBSan) where toolchain supports it.
  - ThreadSanitizer (TSan) optional for concurrency-focused work.

### D) Format/include hygiene
- Add `.clang-format` policy check in CI.
- Optionally add include hygiene tooling (`include-what-you-use`) once baseline is stable.

### E) Security scanning
- Add GitHub CodeQL for C/C++.
- Enable secret scanning (or integrate `gitleaks`).
- Add dependency/vulnerability checks for third-party updates.

## Recommended Policy Guardrails
- Prefer RAII and `std::unique_ptr` for ownership.
- Raw pointers are non-owning unless explicitly documented.
- Document ownership/lifetime semantics in public APIs.
- Treat all external inputs (CLI/file/network) as untrusted.
- Require minimal diffs; avoid unrelated refactors in same PR.
- Human review mandatory for lifetime/concurrency/security-sensitive changes.

## CI Gate Definition (Target State)
Every PR should pass all of the following:
1. Configure + build (primary preset(s)).
2. Unit/integration tests.
3. `clang-tidy` clean on changed files (or defined scope).
4. Sanitizer job (at least ASan debug).
5. CodeQL scan.

## Concrete Implementation Plan (Next Session)

1. Update CMake warning/hardening defaults
- Touch:
  - root `CMakeLists.txt`
  - possibly target-specific CMake files under `src/Engine` and `src/Sandbox`
- Add target-scoped warning settings and CI-only `Werror` switch.

2. Add analysis/sanitizer presets
- Touch:
  - `CMakePresets.json`
- Add presets for:
  - analysis (`clang-tidy`/`/analyze`)
  - sanitizer builds (ASan, optional UBSan)

3. Add CI workflows
- Add/modify under `.github/workflows/`:
  - build-and-test workflow
  - static-analysis workflow
  - CodeQL workflow
- Include matrix strategy for VS + Ninja where practical.

4. Add developer reference docs
- Touch:
  - `README.md` (new "Quality Gates" section)
  - optional `docs/` page for local commands and troubleshooting

5. Validate locally
- Build and smoke test with existing tasks.
- Run targeted checks before enabling strict PR enforcement.

## Suggested Tooling to Add
- Core: `clang-tidy`, `cppcheck`, ASan, CodeQL.
- Optional next phase: UBSan/TSan, IWYU, fuzzing (`libFuzzer`) for parser/input surfaces.

## Suggested Local Command Patterns
Use task-based flows first in VS Code.

- Configure VS: task `Configure VS2026`
- Build VS Debug: task `Build VS2026 Debug`
- Smoke run: task `Run Sandbox VS2026`
- Ninja path available via existing `Configure Ninja` / `Build Ninja` tasks

For CMake Tools-based builds/tests, prefer active preset selection (repo guidance prefers `vs2026-debug` where applicable).

## Risks / Open Decisions
- Whether to enforce `-Werror`/`/WX` for all local builds or CI only.
- Whether sanitizer support should be required on Windows-only toolchain or added on Linux CI too.
- Initial `clang-tidy` scope:
  - changed files only (faster adoption), or
  - full repo (stronger but noisier first rollout).
- Whether to include `cppcheck` in blocking gate or informational-only initially.

## Definition of Done for this initiative
- CI blocks regressions on build/test/analysis/sanitizer/security scan.
- New PRs follow ownership/lifetime and input-validation rules.
- README documents local developer workflow for these gates.
- Team can reproduce checks locally with minimal setup.

## Fast Resume Checklist
When resuming this work:
1. Confirm current CI workflows and whether any guardrails already exist.
2. Add/adjust CMake warning flags and presets first.
3. Introduce `clang-tidy` and sanitizer jobs in non-blocking mode.
4. Fix top-priority findings in touched modules.
5. Flip jobs to required/blocking once baseline is green.
