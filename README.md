# EngineTest

Minimal C++ scaffold for a 3D game engine project with FBX loading and selectable modern rendering backends.

## Project Layout

- `src/Engine`: Static library for engine code
- `src/Sandbox`: Executable used to test engine features
- `tests`: Unit and integration test targets plus developer-owned test scaffold

## Current Features

- FBX model loading via `Assimp`
- Renderer backends for `DirectX 12` and `Vulkan` (through SDL3 renderer driver selection)
- GUI controls via `Dear ImGui`
- Native file picker integration via `nativefiledialog-extended`
- Interactive model rotation and camera distance controls

## Requirements

- CMake 3.26+
- A C++20 compiler
  - Visual Studio 2026 (MSVC) on Windows
  - Clang or GCC on Linux/macOS
- Internet access during first configure/build (dependencies are pulled with CMake `FetchContent`)

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

## Runtime Usage

1. Start `Sandbox`.
2. Click **Load FBX** in the **Model Viewer** window.
3. Select an `.fbx` file.
4. Rotate the model with:
  - left-mouse drag in empty viewport area, or
  - Yaw/Pitch/Roll sliders.
5. Adjust camera distance with the slider.

If DirectX 12 renderer creation fails on the machine, the app automatically falls back to Vulkan.

## Build (Ninja)

On Windows with MSVC, run this from **Developer PowerShell for VS 2026** so the compiler environment is initialized.

```powershell
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
.\build-ninja\src\Sandbox\Sandbox.exe
```

## Run Tests

CTest targets are enabled by default when `ENGINE_BUILD_TESTS=ON`.

Visual Studio generator:

```powershell
cmake -S . -B build-vs -G "Visual Studio 18 2026" -A x64
cmake --build build-vs --config Debug
ctest --test-dir build-vs -C Debug --output-on-failure
```

Ninja generator:

```powershell
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

Included test targets:

- `EngineUnitTests`: unit checks for core data model behavior
- `EngineIntegrationTests`: integration checks for FBX loading against repository assets
- `HumanDeveloperTests`: scaffold project reserved for manually authored developer tests

## Next Steps

1. Add a platform layer (window/input abstraction)
2. Add renderer module (OpenGL/Vulkan/DirectX)
3. Add ECS, asset pipeline, and scene system
