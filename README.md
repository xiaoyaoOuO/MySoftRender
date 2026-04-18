# mySoftRender

mySoftRender 是一个基于 C++20 的 CPU 软光栅渲染器。

当前工程已具备实时窗口渲染、阴影、天空盒、IBL（Diffuse + Specular）与运行时调试面板。

## 当前能力概览

- 渲染管线
  - CPU 光栅化（MSAA 1x/2x/4x）
  - 背面剔除与线框叠加切换
  - 片元着色支持单线程/线程池并行切换
- 场景与模型
  - 支持 `Triangle` / `Cube` / `Sphere` / `MeshObject(OBJ)`
  - 支持对象级纹理绑定（每个对象可使用独立贴图）
  - 内置两个场景预设（Mary + Floor + Point Light / Sphere + Skybox）
- 光照与阴影
  - 世界空间 Blinn-Phong（环境光、漫反射、高光、自发光）
  - 阴影 Pass
    - 点光源：6 面深度图
    - 非点光路径：单张 2D 深度图
  - 阴影过滤：当前有效为 Hard 与 PCF（PCSS 参数已预留）
- 天空盒与 IBL
  - 自动扫描 `assets/cubemap/*` 并加载可用天空盒
  - 支持 Diffuse IBL 五级预卷积采样（`<SkyboxName>_cov/lod0~lod4`，支持缺档最近回退）
  - 支持 Specular IBL（Specular LOD / prefilter / BRDF LUT，可回退）
- 调试 UI（Dear ImGui）
  - Tab：Skybox / Shadow / Light / Model / Threading / Status
  - 支持运行时调节阴影、IBL、光源、模型参数

## 依赖

- 必需
  - SDL2
  - CMake 3.16+
  - 支持 C++20 的编译器
- 通过 FetchContent 自动拉取
  - GLM
  - stb
  - Dear ImGui

## 目录结构

- `include/`：头文件
- `src/`：渲染与主循环实现
- `assets/`：模型、贴图、天空盒资源
- `utility/`：离线工具模块（当前包含天空盒模糊采样 LUT 生成器与命令行烘焙入口）

## 构建与运行

### 通用 CMake

```bash
cmake -S . -B build
cmake --build build -j
```

Release：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

离线工具（仅构建 skybox LUT 烘焙器）：

```bash
cmake --build build --target skyboxLutBaker -j
```

离线工具（仅构建 specular 六级卷积烘焙器）：

```bash
cmake --build build --target skyboxSpecularLodBaker -j
```

### Windows（MSYS2 MinGW）

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe
cmake --build build -j
```

Release：

```bash
cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe -DMYSOFTR_RENDER_OFFLINE_DEPS=OFF
cmake --build build-release -j
```

运行（Windows）：

```bash
./build/mySoftRender.exe
```

## 运行时操作

- 键盘
  - `W/A/S/D`：相机平面移动
  - `F1`：线框叠加开关
  - `F2`：背面剔除开关
  - `F3`：MSAA 档位循环（1x/2x/4x）
  - `F4`：调试面板显示/隐藏
  - `F5`：鼠标捕获开关
  - `ESC`：退出
- 鼠标
  - 捕获开启时：相对移动控制视角

## 资源约定（天空盒与 IBL）

天空盒目录示例：

- `assets/cubemap/<SkyboxName>/posx.* ... negz.*`

可选 IBL 资源：

- `assets/cubemap/<SkyboxName>/ibl/irradiance/`（六面图）
- `assets/cubemap/<SkyboxName>/<SkyboxName>_cov/lod0~lod4/`（每个 lod 目录内 6 张面图：`posx/negx/posy/negy/posz/negz.png`）
  - 运行时优先使用该路径。
  - 兼容旧路径：`assets/cubemap/<SkyboxName>/ibl/diffuse_lod/lod0~lod4/`。
- `assets/cubemap/<SkyboxName>/ibl/prefilter/`（六面图）
- `assets/cubemap/<SkyboxName>/ibl/brdf_lut.png`（或 `brdf_lut.jpg` / `brdfLUT.png` / `brdf.png`）

Specular LOD（可选，推荐）：

- `assets/cubemap/<SkyboxName>/ibl/specular_lod/lod0~lod4/`（每个 lod 目录内 6 张面图）
- 兼容路径：`assets/cubemap/<SkyboxName>/ibl/prefilter_lod/lod0~lod4/`
- 兼容路径：`assets/cubemap/<SkyboxName>/<SkyboxName>_cov/lod0~lod4/`
- 兼容旧路径：`assets/cubemap/<SkyboxName>/ibl/diffuse_lod/lod0~lod4/`

Specular IBL 运行时回退链：

1. 命中 `Specular LOD`（`IBLSpecLodMaps[requestedLod]`）
2. 否则命中 `ibl/prefilter`（`sampleLod`）
3. 否则命中 `skybox`（`sampleLod`）
4. 否则返回黑色（0）

## Specular 六级卷积离线生成（utility）

新增工具：

- `utility/SkyboxSpecularLodBakerMain.cpp`

构建后可执行文件：

- Windows: `build/skyboxSpecularLodBaker.exe`
- Linux/macOS: `build/skyboxSpecularLodBaker`

单天空盒示例：

```bash
./build/skyboxSpecularLodBaker --cubemap-dir assets/cubemap/CornellBox
```

批量处理全部天空盒：

```bash
./build/skyboxSpecularLodBaker --cubemap-root assets/cubemap --size 64 --samples "1,64,96,128,160,192"
```

说明：

- 默认输出目录：`assets/cubemap/<SkyboxName>/<SkyboxName>_cov/`。
- 固定生成 6 档 `lod0~lod5`，每档 6 张 PNG。
- 采样默认参数：样本数 `1,64,96,128,160,192`；卷积锥角（度）`0,10,18,30,45,62`。
- 可通过 `--samples`、`--angles-deg` 覆盖默认值。

## 模糊采样 LUT 生成（utility）

当前已提供离线工具模块：

- `utility/SkyboxLutGenerator.h`
- `utility/SkyboxLutGenerator.cpp`
- `utility/SkyboxLutBakerMain.cpp`

说明：

- 该模块是离线工具，不在运行时渲染循环中执行。
- 生成结果是 2D LUT（`x=roughness`，`y=NdotV`），输出 RGB8。
- 当前导出格式为 PPM(P6)，可直接作为离线采样图。

### 离线命令行使用

构建后可执行文件：

- Windows: `build/skyboxLutBaker.exe`
- Linux/macOS: `build/skyboxLutBaker`

命令示例：

```bash
./build/skyboxLutBaker --cubemap-dir assets/cubemap/Skybox --output output/skybox_blur_lut.ppm --width 128 --height 128 --samples 64 --blur-min 0.01 --blur-max 0.45
```

参数说明：

- `--cubemap-dir`：天空盒目录（需包含 posx/negx/posy/negy/posz/negz）
- `--output`：输出 RGB 图路径（PPM）
- `--width`：LUT 宽度（默认 128）
- `--height`：LUT 高度（默认 128）
- `--samples`：模糊采样数（默认 64）
- `--blur-min`：最小模糊半径（默认 0.01）
- `--blur-max`：最大模糊半径（默认 0.45）

### 代码方式使用（可选）

如果你希望在其他离线流程中复用该模块，可直接调用 API：

示例：

```cpp
#include "Texture.h"
#include "utility/SkyboxLutGenerator.h"

CubemapTexture skybox;
if (!skybox.loadFromDirectory("assets/cubemap/Skybox")) {
    return;
}

utility::SkyboxLutBuildConfig cfg;
cfg.width = 128;
cfg.height = 128;
cfg.sampleCount = 64;
cfg.blurRadiusMin = 0.01f;
cfg.blurRadiusMax = 0.45f;

utility::SkyboxLutImage lut = utility::SkyboxLutGenerator::GenerateFromSkybox(skybox, cfg);
if (!lut.valid()) {
    return;
}

utility::SkyboxLutGenerator::SaveAsPPM(lut, "output/skybox_blur_lut.ppm");
```

当前工程已内置 `skyboxLutBaker` 目标，无需手动把烘焙逻辑接入实时渲染主程序。

## VS Code 任务

工程已提供常用任务：

- `cmake configure`
- `cmake build`
- `cmake configure release`
- `cmake build release`
- `run soft renderer`
- `run soft renderer release`

## 备注

- 当前仓库未配置自动化单元测试（`enable_testing/add_test` 未接入）。
- README 以当前代码实现为准，后续新增功能请同步更新本文档。
