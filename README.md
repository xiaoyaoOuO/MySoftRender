# mySoftRender

mySoftRender 是一个基于 C++20 的 CPU 软光栅渲染器，当前实现已覆盖实时窗口渲染、阴影、天空盒、IBL 与运行时调试。

## 核心能力

- 渲染基础
  - CPU 光栅化、透视正确插值、ZBuffer
  - MSAA 1x/2x/4x 切换
  - 背面剔除与线框叠加
- 场景与模型
  - `Triangle` / `Cube` / `Sphere` / `MeshObject(OBJ)`
  - 对象级纹理与对象级材质（albedo/roughness/metallic）
  - 内置两套场景预设（Scene1: Mary+地板+点光源; Scene2: 无纹理球体+天空盒+IBL）
  - 默认片元着色器：CookTorrance（含 IBL 合成）
- 光照与阴影
  - 世界空间光照（含直射 + 环境）
  - 点光阴影（6 面深度）与非点光阴影（2D 深度）
  - 阴影过滤模式：Hard / PCF / PCSS（参数入口已接入）
- Skybox 与 IBL
  - 自动扫描 `assets/cubemap/*` 并加载天空盒
  - Diffuse IBL（irradiance cubemap，缺失时回退 skybox 模糊采样）
  - Specular IBL（Split-Sum：Specular LOD + BRDF LUT）
- 调试与并行
  - Dear ImGui 面板（Scene 下拉切换 + Skybox/Shadow/Light/Model/Threading/Status 六个 Tab）
  - 片元着色单线程/线程池并行切换
  - 实时帧率与线程池统计显示
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

- `cmake configure` — Debug 构建配置
- `cmake build` — Debug 构建（默认构建任务，快捷键 `Ctrl+Shift+B`）
- `cmake configure release` — Release 构建配置
- `cmake build release` — Release 构建
- `run soft renderer` — 构建并运行 Debug 版
- `run soft renderer release` — 构建并运行 Release 版

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
  - 缺失时回退 skybox 的方向模糊采样（`sampleLod`），无需额外资源
- Specular IBL（6 档 lod0~lod5）
  - 优先：`assets/cubemap/<SkyboxName>/<SkyboxName>_cov/lod0~lod5/`
  - 兼容：`assets/cubemap/<SkyboxName>/ibl/specular_lod/lod0~lod5/`
  - 兼容：`assets/cubemap/<SkyboxName>/ibl/prefilter_lod/lod0~lod5/`
- 预过滤回退源
  - `assets/cubemap/<SkyboxName>/ibl/prefilter/`
  - 缺失时回退 skybox 的 `sampleLod`，最终回退黑色
- BRDF LUT（2D 纹理）
  - 推荐命名：`assets/cubemap/<SkyboxName>/ibl/<SkyboxName>_lut.png`
  - 兼容命名（按优先级）：`<SkyboxName>_lut.*`、`skybox_lut.*`、`brdf_lut.*`、`brdfLUT.png`、`brdf.png`
  - 缺失时使用解析近似函数替代

### Specular IBL 运行时回退链

1. 命中 Specular LOD（请求档位缺失时回退最近可用档）
2. 回退 `ibl/prefilter` 的 `sampleLod`
3. 回退 skybox 的 `sampleLod`
4. 最终回退黑色（0）

## 离线工具

### 1) skyboxSpecularLodBaker

作用：对单个或批量天空盒生成 6 档 Specular LOD 立方体贴图（`lod0~lod5`），lod0 无模糊，lod1~lod5 逐级增大卷积锥角。

单天空盒：

```bash
./build/skyboxSpecularLodBaker --cubemap-dir assets/cubemap/CornellBox
```

批量：

```bash
./build/skyboxSpecularLodBaker --cubemap-root assets/cubemap --size 64 --samples "1,64,96,128,160,192"
```

主要参数：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--cubemap-dir <dir>` | （必选其一） | 单天空盒目录 |
| `--cubemap-root <dir>` | （必选其一） | 批量处理根目录下所有天空盒 |
| `--size <int>` | 64 | 每面输出分辨率（正方形） |
| `--samples <csv>` | `1,64,96,128,160,192` | lod0~lod5 各级采样数 |
| `--angles-deg <csv>` | `0,10,18,30,45,62` | lod0~lod5 各级锥角（度） |
| `--seed <uint>` | 随机 | 固定随机数种子 |
| `--help` | — | 打印帮助信息 |

输出布局：`<SkyboxDir>/<SkyboxName>_cov/lod0~lod5/{posx,negx,posy,negy,posz,negz}.png`

### 2) skyboxLutBaker

作用：生成 Split-Sum BRDF 积分 LUT（R=scale, G=bias, B=0），横轴 `x=NdotV`，纵轴 `y=roughness`。该 LUT 与具体天空盒无关，是纯数学积分结果。

> **注意**：`--cubemap-dir` 参数仅为历史兼容保留，BRDF LUT 生成不依赖天空盒内容。

示例：

```bash
./build/skyboxLutBaker --cubemap-dir assets/cubemap/Skybox --output output/skybox_lut.png --width 128 --height 128 --samples 64
```

主要参数：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--cubemap-dir <dir>` | （必填） | 历史兼容，不影响输出结果 |
| `--output <path>` | （必填） | 输出路径，支持 `.png` / `.ppm` |
| `--width <int>` | 128 | LUT 宽度 |
| `--height <int>` | 128 | LUT 高度 |
| `--samples <int>` | 64 | BRDF 积分采样数 |
| `--blur-min <float>` | — | 历史遗留，已忽略 |
| `--blur-max <float>` | — | 历史遗留，已忽略 |
| `--help` | — | 打印帮助信息 |

## 当前工程状态

- 构建系统：CMake 3.16+，Win64 MSYS2 MinGW 为主要开发环境
- 当前未接入自动化测试（`enable_testing/add_test`）
- 当前未配置独立 lint 目标
- 编译标准：C++20，Release 启用 `-O3 -march=native -ffast-math`
- 代码注释规范：中文 Doxygen（`@brief` / `@param` / `@return`），详见 `Restrict.md`
