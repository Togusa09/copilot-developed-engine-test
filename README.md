# EngineTest

Minimal C++ scaffold for a 3D game engine project.

## Project Layout

- `src/Engine`: Static library for engine code
- `src/Sandbox`: Executable used to test engine features

## Requirements

- CMake 3.26+
- A C++20 compiler
  - Visual Studio 2026 (MSVC) on Windows
  - Clang or GCC on Linux/macOS

### Windows setup (winget)

Install the required tools on Windows with:

```powershell
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
```

Optional: install Visual Studio Community (IDE + MSVC toolchain) instead of Build Tools:

```powershell
winget install --id Microsoft.VisualStudio.2022.Community -e
```

### Verify installs (Windows)

Open a new terminal after installation, then run:

```powershell
cmake --version
ninja --version
where.exe cl
```

If `cl` is not found, open the **x64 Native Tools Command Prompt for VS 2026** and run the same checks there.

## Build (Windows, Visual Studio)

```powershell
cmake -S . -B build-vs -G "Visual Studio 18 2026" -A x64
cmake --build build-vs --config Debug
.\build-vs\src\Sandbox\Debug\Sandbox.exe
```

Run these commands from **Developer PowerShell for VS 2026** (or after the MSVC toolchain is available in your PATH).

## Build (Ninja)

On Windows with MSVC, run this from **Developer PowerShell for VS 2026** so the compiler environment is initialized.

```powershell
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
.\build-ninja\src\Sandbox\Sandbox.exe
```

## Next Steps

1. Add a platform layer (window/input abstraction)
2. Add renderer module (OpenGL/Vulkan/DirectX)
3. Add ECS, asset pipeline, and scene system
