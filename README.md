# mySoftRender

mySoftRender 是一个基于 C++20 的 CPU 软光栅渲染器，当前实现已覆盖实时窗口渲染、阴影、天空盒、IBL 与运行时调试。

## 核心能力

- 渲染基础
  - CPU 光栅化、透视正确插值、ZBuffer
  - MSAA 1x/2x/4x 切换
  - 背面剔除与线框叠加
- 场景与模型
  - `Triangle` / `Cube` / `Sphere` / `MeshObject(OBJ)`
  - 对象级纹理与对象级材质（baseColor/roughness/metallic）
  - 内置两套场景预设
- 光照与阴影
  - 世界空间光照（含直射 + 环境）
  - 点光阴影（6 面深度）与非点光阴影（2D 深度）
  - 阴影过滤模式：Hard / PCF / PCSS（参数入口已接入）
- Skybox 与 IBL
  - 自动扫描 `assets/cubemap/*` 并加载天空盒
  - Diffuse IBL（irradiance 或 skybox 回退）
  - Specular IBL（Split-Sum：Specular LOD + BRDF LUT）
- 调试与并行
  - Dear ImGui 面板（Skybox/Shadow/Light/Model/Threading/Status）
  - 片元着色单线程/线程池并行切换
- 离线工具
  - `skyboxSpecularLodBaker`：生成 6 档 Specular LOD cubemap
  - `skyboxLutBaker`：生成 BRDF LUT（PNG/PPM）

## 目录结构

- `include/`：头文件
- `src/`：主程序与渲染实现
- `utility/`：离线工具与生成器
- `assets/`：模型、贴图、天空盒资源

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

运行：

```bash
./build/mySoftRender.exe
```

### 常用构建目标

```bash
cmake --build build --target mySoftRender -j
cmake --build build --target skyboxSpecularLodBaker -j
cmake --build build --target skyboxLutBaker -j
```

### VS Code 任务

- `cmake configure`
- `cmake build`
- `cmake configure release`
- `cmake build release`
- `run soft renderer`
- `run soft renderer release`

## 运行时操作

- 键盘
  - `W/A/S/D`：相机移动
  - `F1`：线框叠加开关
  - `F2`：背面剔除开关
  - `F3`：MSAA 档位循环
  - `F4`：调试面板显示/隐藏
  - `F5`：鼠标捕获开关
  - `ESC`：退出
- 鼠标
  - 捕获开启时，移动鼠标控制视角

## 资源约定（天空盒与 IBL）

### 天空盒目录

```text
assets/cubemap/<SkyboxName>/
  posx.* negx.* posy.* negy.* posz.* negz.*
```

### IBL 可选资源

- Diffuse IBL
  - `assets/cubemap/<SkyboxName>/ibl/irradiance/`
- Specular IBL（6 档）
  - 优先：`assets/cubemap/<SkyboxName>/<SkyboxName>_cov/lod0~lod5/`
  - 兼容：`assets/cubemap/<SkyboxName>/ibl/specular_lod/lod0~lod5/`
  - 兼容：`assets/cubemap/<SkyboxName>/ibl/prefilter_lod/lod0~lod5/`
- 预过滤回退源
  - `assets/cubemap/<SkyboxName>/ibl/prefilter/`
- BRDF LUT（2D）
  - 推荐命名：`assets/cubemap/<SkyboxName>/ibl/<SkyboxName>_lut.png`
  - 兼容命名：`skybox_lut.*`、`brdf_lut.*`、`brdfLUT.png`、`brdf.png`

### Specular IBL 运行时回退链

1. 命中 Specular LOD（请求档位缺失时回退最近可用档）
2. 回退 `ibl/prefilter` 的 `sampleLod`
3. 回退 skybox 的 `sampleLod`
4. 最终回退黑色（0）

## 离线工具

### 1) skyboxSpecularLodBaker

作用：对单个或批量天空盒生成 6 档 Specular LOD 立方体贴图（`lod0~lod5`）。

单天空盒：

```bash
./build/skyboxSpecularLodBaker --cubemap-dir assets/cubemap/CornellBox
```

批量：

```bash
./build/skyboxSpecularLodBaker --cubemap-root assets/cubemap --size 64 --samples "1,64,96,128,160,192"
```

### 2) skyboxLutBaker

作用：生成 Split-Sum BRDF LUT（`x=roughness`，`y=NdotV`）。

示例：

```bash
./build/skyboxLutBaker --cubemap-dir assets/cubemap/Skybox --output output/skybox_lut.png --width 128 --height 128 --samples 256
```

## 当前工程状态

- 当前 CMake 未接入自动化测试（`enable_testing/add_test`）。
- 当前仓库未配置独立 lint 目标。
