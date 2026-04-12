# Copilot Instructions for `mySoftRender`

## Build, test, and lint commands

This repository uses CMake (C++20) and builds a single executable: `mySoftRender`.

### Cross-platform CMake commands

```bash
cmake -S . -B build
cmake --build build -j
```

Release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

### Windows (MSYS2 MinGW) commands used by the repo

The checked-in VS Code tasks use these explicit settings:

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe
cmake --build build -j
```

Release variant:

```bash
cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe -DMYSOFTR_RENDER_OFFLINE_DEPS=OFF
cmake --build build-release -j
```

### Test and lint status

- No automated unit/integration tests are currently defined in CMake (`enable_testing`/`add_test` not present).
- No lint target/tooling is configured in this repository.

## High-level architecture

`main.cpp` sets up SDL window/texture presentation, creates scene content (camera, mesh objects, lights), and drives input + frame loop. CPU rendering happens in `SoftwareRenderer`; final color buffer is uploaded to an SDL `ARGB8888` streaming texture each frame.

Rendering is split into two passes inside `SoftwareRenderer::DrawScene`:

1. Shadow/depth pass from light view: project triangles with light VP and write per-light depth into `Light::lightViewDepths()`.
2. Camera pass: transform objects by camera VP, rasterize triangles via `Rasterizer`, then run fragment shading (custom shader if set, otherwise direct color write).

Key data flow:

- `Scene` owns `objects`, `camera`, and `lights` (`unique_ptr` containers).
- `Object` provides transform + optional per-object texture (`shared_ptr<Texture2D>`).
- `MeshObject` is the primary mesh path for OBJ data (`ObjMeshData` from `ObjLoader`).
- `Rasterizer` outputs `Fragment` records (depth/color/normal/worldPos/shadowVisibility) consumed by fragment shader callback.

## Key conventions in this codebase

- **Screen-space winding/culling convention is fixed:** with Y-down viewport mapping, front faces are `signedArea > 0` in `Rasterizer::Rasterize_Triangle`. Keep this sign unless you intentionally change winding.
- **OBJ + texture coordinate convention:** `ObjLoader` flips V on import (`uv.y = 1 - uv.y`). `Texture2D::sample` wraps U and clamps V. `Rasterizer` also applies seam fixups when U crosses 0/1 boundary.
- **Conservative clip reject before perspective divide:** projection helpers reject triangles if any vertex has `w <= epsilon` or all 3 vertices lie outside one clip plane. This avoids behind-camera mirroring artifacts.
- **Per-object texture ownership:** textures belong to `Object`/`MeshObject`, not global renderer state. In draw path, pass `obj->hasTexture() ? obj->texture().get() : nullptr`.
- **Matrix caching pattern:** `Camera` and `Object` cache computed matrices behind dirty flags; update through setters/transform helpers to keep caches valid.
- **Comments and documentation language:** repository convention is Chinese explanatory comments for non-trivial logic; follow existing Chinese inline comment style when adding logic.
