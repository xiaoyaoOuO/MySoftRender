# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

mySoftRender is a C++20 CPU soft rasterizer with real-time window rendering, shadow mapping, skybox, IBL (image-based lighting), and runtime debugging via Dear ImGui.

## Build & Run

**Environment**: MSYS2 MinGW-w64 on Windows. The compiler and make paths assume `C:/msys64/mingw64/bin/`. All commands should be run in a MinGW-aware shell where `cmake`, `g++`, and `mingw32-make` are on PATH.

### Debug build

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe
cmake --build build -j
./build/mySoftRender.exe
```

### Release build

```bash
cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe -DMYSOFTR_RENDER_OFFLINE_DEPS=OFF
cmake --build build-release -j
```

### Build specific targets

```bash
cmake --build build --target mySoftRender -j
cmake --build build --target skyboxSpecularLodBaker -j
cmake --build build --target skyboxLutBaker -j
```

### Offline tools

```bash
# Generate specular LOD cubemaps for a skybox
./build/skyboxSpecularLodBaker --cubemap-dir assets/cubemap/CornellBox

# Generate BRDF LUT texture
./build/skyboxLutBaker --cubemap-dir assets/cubemap/Skybox --output output/skybox_lut.png --width 128 --height 128 --samples 256
```

### CMake options

- `MYSOFTR_RENDER_DISABLE_DEP_UPDATES=ON` — skip FetchContent git updates during configure
- `MYSOFTR_RENDER_OFFLINE_DEPS=ON` — use only already-populated FetchContent deps (offline mode)

## Dependencies

All fetched automatically via CMake `FetchContent`:

| Library | Source | Purpose |
|---------|--------|---------|
| SDL2 | `find_package` (system) | Window, input, event loop |
| glm | gitee mirror, v1.0.1 | Vector/matrix math |
| stb | gitee mirror, master | Image loading (`stb_image`) |
| Dear ImGui | gitee mirror, v1.90.9 | Debug UI panels |

No tests, CI, or lint are configured.

## Architecture

### Three executable targets (defined in `CMakeLists.txt`)

1. **`mySoftRender`** — The main interactive renderer (SDL2 window, real-time rendering with ImGui debug panels)
2. **`skyboxLutBaker`** — CLI tool to generate BRDF LUT images
3. **`skyboxSpecularLodBaker`** — CLI tool to generate specular LOD cubemaps

### Rendering pipeline (in `src/software_renderer.cpp`)

The `SoftwareRenderer::DrawScene()` method orchestrates the full frame:

1. **Shadow pass** — For each shadow-casting light, render depth from the light's perspective into `Light::lightViewDepths()`. Point lights do 6-face cubemap depth.
2. **Skybox background** — Fill pixels not covered by geometry using pre-cached skybox view-ray directions (`skyboxViewRaysCache_`), optionally respecting the depth buffer.
3. **Main camera pass** — For each object in `Scene::objects`, transform vertices through model→view→projection, rasterize triangles, execute the fragment shader (with optional thread-pool parallelism).

### Key classes and their roles

- **`SoftwareRenderer`** (`include/software_renderer.h`) — Top-level renderer. Owns the color buffer, `Rasterizer`, and `RenderThreadPool`. Accepts a user-provided `fragmentShader_` callback.
- **`Rasterizer`** (`include/Rasterizer.h`) — Triangle rasterization with MSAA (1x/2x/4x), depth testing, perspective-correct interpolation, and backface culling. Contains `RasterizeTriangleMSAA()` — the core hot path.
- **`Scene`** (`include/Scene.h`) — Singleton (`Scene::instance`) holding all scene state: objects, lights, camera, skybox, IBL settings, shadow settings. Most runtime state lives here and is mutated by the ImGui debug panel.
- **`Object`** base class → **`MeshObject`** (OBJ meshes), **`Sphere`**, **`Cube`**, **`Triangle`**. Each has position/rotation/scale, a shared texture pointer, a `Material` pointer, and a `castShadow_` flag.
- **`Light`** (`include/Light.h`) — Point, Directional, or Spot. Manages its own shadow matrices and depth buffers. Point lights maintain 6-face cubemap depth.
- **`Camera`** (`include/Camera.h`) — FPS-style camera with WASD movement and mouse look. Uses dirty-flag caching for view/projection matrices.
- **`DebugUI`** (`include/DebugUI.h`) — Wraps Dear ImGui with tabbed panels: Skybox, Shadow, Light, Model, Threading, Status. Handles scene-switch requests and skybox-switch requests.
- **`RenderThreadPool`** (`include/RenderThreadPool.h`) — Generic thread pool with `enqueue()`, `parallelFor()`, and `waitIdle()`. Used for parallel fragment shading.
- **`Texture2D`** / **`CubemapTexture`** (`include/Texture.h`) — 2D texture with bilinear sampling; cubemap with `sample()` and `sampleLod()`.
- **`Material`** (`include/Material.h`) — Simple struct: `albedo`, `metallic`, `roughness`. Used by the fragment shader for IBL composition.

### Data flow: main.cpp → rendering

`main.cpp` (~63K, the largest file) handles the main loop:
1. SDL event processing + keyboard/mouse input → updates `Camera` and feature toggles
2. `DebugUI::beginFrame()` → `drawShadowPanel()` → `render()`
3. Scene preset logic (two built-in presets), skybox discovery from `assets/cubemap/*`
4. Fragment shader lambdas: one for objects, one for light-proxy spheres — both compose Blinn-Phong direct lighting + shadow visibility + IBL (diffuse + specular split-sum)
5. Calls `SoftwareRenderer::DrawScene()`

### IBL resource discovery

Skyboxes live in `assets/cubemap/<Name>/`. The IBL system auto-discovers:
- Irradiance maps at `assets/cubemap/<Name>/ibl/irradiance/`
- Specular LOD at `assets/cubemap/<Name>/<Name>_cov/lod0~lod5/`
- BRDF LUT at `assets/cubemap/<Name>/ibl/<Name>_lut.png`

## Coding Conventions (from `Docs/guidelines/Restrict.md`)

- **All code must have Chinese comments**. Use `@brief` for function/class Doxygen summaries. Functions returning void don't need `@return`; skip it if `@brief` and `@return` would be redundant.
- **Function body comments**: add a one-line comment above important blocks explaining what that section does.
- **Classes/structs**: use `@brief` to summarize their purpose.
- **Error documentation**: record mistakes and solutions in `Docs/dev-logs/Experience.md` (check it first when encountering errors).
- **Workspace isolation**: all work is confined to this directory. Any operations outside it require explicit user approval.
- **Log maintenance**: always append to the end of files — never overwrite existing content.
