# Copilot Instructions for EngineTest (C++ Game Engine)

## Project Context

- This repository is a C++20 game engine scaffold built with CMake.
- Primary targets:
  - `Engine` (static library in `src/Engine`)
  - `Sandbox` (executable in `src/Sandbox`)
- Platform focus starts with Windows, but keep code portable when practical.

## High-Level Architecture

- Keep engine code in `src/Engine` and app/demo code in `src/Sandbox`.
- Prefer clear module boundaries (e.g., Core, Platform, Renderer, ECS, Assets, Scene).
- Do not leak platform-specific headers/types into public engine interfaces.
- Favor composition over inheritance for gameplay/runtime systems.

## Coding Style

- Use modern C++20 features when they improve clarity and safety.
- Avoid one-letter names except simple loop indices.
- Keep functions short and single-purpose.
- Use `PascalCase` for types/classes, `camelCase` for functions/variables, and `snake_case` only where existing code already uses it.
- Avoid inline comments unless intent is non-obvious; prefer self-explanatory names.

## Performance and Memory

- Prioritize deterministic performance and low allocations in per-frame paths.
- Avoid heap allocations inside hot loops unless clearly justified.
- Prefer stack allocation, object pools, or preallocated buffers for runtime systems.
- Pass heavy objects by reference (`const&` when read-only).
- Be explicit about ownership; prefer `std::unique_ptr` over shared ownership.

## Error Handling and Logging

- Fail fast on unrecoverable initialization errors.
- Use explicit return values/status types for recoverable runtime failures.
- Do not use exceptions for normal control flow.
- Route diagnostics through a central logging mechanism (add one if missing before expanding ad-hoc `std::cout` usage).

## CMake and Build Rules

- Keep root `CMakeLists.txt` minimal; define module-specific behavior in subdirectories.
- Every new module must be added as a target with explicit include dirs and link dependencies.
- Keep warning levels high (`/W4` or `-Wall -Wextra -Wpedantic`) and fix warnings in touched code.
- Do not introduce non-essential third-party dependencies without clear need.

## Testing and Validation

- Prefer small, testable units in engine subsystems.
- Add tests for non-trivial logic when a test harness exists.
- If no test harness exists, include a minimal verification path via `Sandbox`.
- Validate new runtime paths with clear repro steps in PR/change notes.

## Rendering and Game-Loop Guidance

- Keep the main loop phases explicit: input, simulation, render, present.
- Keep frame timing code centralized and consistent.
- Separate render API abstraction from high-level renderer logic.
- Avoid mixing gameplay logic directly into rendering code.

## Collaboration Rules for Copilot

- Make focused, minimal diffs; avoid refactoring unrelated code.
- Preserve public APIs unless change is requested.
- When adding files, follow existing folder and naming conventions.
- Update `README.md` when setup/build behavior changes.
- When assumptions are required, choose the simplest option and state it clearly.
