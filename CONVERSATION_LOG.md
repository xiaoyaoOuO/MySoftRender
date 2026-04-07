# 对话实现记录

用于记录每一次对话中实现的功能与修改文件。

## 2026-04-01 会话 001

### 实现功能

- 新增 Texture2D：支持从文件加载纹理、程序化棋盘纹理生成、双线性采样。
- 光栅器新增纹理三角形路径：支持 UV 插值并进行纹理采样。
- Sphere 网格新增球面 UV 生成，支持球体贴图采样。
- 渲染器新增球体纹理接口：优先尝试加载 assets/earth.jpg，找不到时自动回退到程序化棋盘纹理。
- 默认关闭线框叠加，程序启动后可直接看到球体纹理（按 F1 可切换线框）。

### 修改文件

- CMakeLists.txt
- include/Sphere.h
- src/Sphere.cpp
- include/Texture.h
- src/Texture.cpp
- include/Rasterizer.h
- src/Rasterizer.cpp
- include/software_renderer.h
- src/software_renderer.cpp
- src/main.cpp

## 2026-04-01 会话 002

### 实现功能

- 修复球体纹理路径查找：支持在不同运行目录下自动查找项目根目录的 assets/earth.jpg。
- 启动时输出实际命中的纹理路径，便于确认加载来源。

### 修改文件

- src/main.cpp

## 2026-04-01 会话 003

### 实现功能

- 重写 4x MSAA：从近似覆盖率方案改为每像素 4 个 sample 独立深度/颜色缓存。
- 纯色三角形与纹理三角形统一走同一套 MSAA 光栅化流程。
- 解析阶段按 4 个 sample 平均颜色输出像素，深度取最前 sample，修复边缘深度/颜色不一致。

### 修改文件

- include/Rasterizer.h
- src/Rasterizer.cpp

## 2026-04-01 会话 004

### 实现功能

- 将抗锯齿实现调整为传统 MSAA：sample 级覆盖与深度测试，像素级单次着色。
- 移除逐 sample 着色路径，降低纹理采样与着色开销。
- 对边缘像素使用覆盖 sample 的重心点进行一次着色，再写回通过深度测试的 samples。

### 修改文件

- src/Rasterizer.cpp

## 2026-04-01 会话 005

### 实现功能

- 新增独立 OBJ 加载模块（不接入当前渲染流程）。
- 支持解析 v/vt/vn/f，支持负索引，支持多边形面扇形三角化。
- 输出统一网格数据结构（顶点、三角形索引、包围盒、法线/UV可用标记）。
- 当 OBJ 缺少法线时自动生成顶点法线。

### 修改文件

- include/ObjLoader.h
- src/ObjLoader.cpp
- CMakeLists.txt

## 2026-04-01 会话 006

### 实现功能

- 为 ObjLoader 模块补充函数级注释（头文件声明与实现文件中的主要辅助函数）。
- 注释覆盖索引解析、面片 token 解析、包围盒计算、缺失法线生成与主加载入口。

### 修改文件

- include/ObjLoader.h
- src/ObjLoader.cpp

## 2026-04-04 会话 007

### 实现功能

- 为 MSAA 实现补充中文函数注释，明确每个函数的作用与调用方式。
- 注释覆盖采样索引映射、深度/颜色/法线解析，以及三角形 MSAA 光栅化主流程。
- 未修改渲染算法与接口，仅增强代码可读性与维护性。

### 修改文件

- src/Rasterizer.cpp

## 2026-04-04 会话 008

### 实现功能

- 修复由于未指定链接器路径与动态库依赖而在 `cmake build`（默认 Debug 模式）下引发 `collect2.exe: error: ld returned 5 exit status` 拒绝访问的编译系统错误。
- 给 `.vscode/tasks.json` 中的 `cmake configure`、`cmake build` 以及 `run soft renderer` 等基础任务显式追加了包含 `C:/msys64/mingw64/bin` 环境依赖的 `PATH` 系统变量，并固定指定了 `MinGW Makefiles` 及其特定编译器构建参数，从而保证了 Debug 构建和 Release 构建行为的一致性。

### 修改文件

- .vscode/tasks.json

## 2026-04-04 会话 009

### 实现功能

- 新增对象级纹理绑定能力：在 `Object` 基类中加入纹理共享指针与绑定/清除/查询接口，使每个对象都可配置独立纹理。
- 渲染流程改为按对象读取纹理：`SoftwareRenderer::DrawScene` 中不再使用全局唯一球体纹理，而是优先读取当前对象绑定纹理进行采样。
- 完整接入场景级纹理分配：在 `main.cpp` 中分别加载 `earth.jpg`（球体）与 `MC003_Kozakura_Mari.png`（Mary），并在建场景时绑定到对应对象。
- 修复并验证编译：补齐 `Triangle.h` 对 `std::array` 的依赖头文件，Debug/Release 两种构建均通过。

### 修改文件

- include/Object.h
- include/Triangle.h
- include/software_renderer.h
- src/software_renderer.cpp
- src/main.cpp

## 2026-04-04 会话 010

### 实现功能

- 新增独立经验复盘文档 `Experience.md`，集中记录已发生错误、根因、解决方案与预防措施。
- 复盘内容覆盖构建环境不一致导致的链接失败、头文件缺失导致的模板类型错误、前向声明误用以及纹理归属设计问题。

### 修改文件

- Experience.md

## 2026-04-04 会话 011

### 实现功能

- 修复 OBJ 贴图方向错误：在 `ObjLoader` 读取 `vt` 时执行 `uv.y = 1.0f - uv.y`，统一 OBJ UV 约定与当前采样坐标系，避免模型贴图上下颠倒。
- 修复纹理接缝误判导致的局部采样扭曲：将接缝修正条件从单纯 `maxU-minU>0.5` 收紧为“同时存在接近 0 与接近 1 的 U 值”后才触发，避免普通 UV 岛被错误平移。
- 完成 Debug/Release 双构建验证，均编译通过。

### 修改文件

- src/ObjLoader.cpp
- src/Rasterizer.cpp

## 2026-04-04 会话 012

### 实现功能

- 对当前 OBJ 渲染性能进行了代码级瓶颈定位与量化分析。
- 确认 `assets/mary/mary.obj` 面片规模约为 10174 个三角形，当前场景组织会将其拆分为 10174 个 `Triangle` 对象逐帧处理。
- 确认构建缓存中 `build` 为 Debug、`build-release` 为 Release，排查“帧率偏低是否受构建配置影响”。
- 输出面向当前工程的优化优先级清单，覆盖：对象组织重构、动态分发移除、MSAA 成本分级、背面剔除、片段写回路径收敛与纹理采样路径优化。

### 修改文件

- Experience.md
- CONVERSATION_LOG.md

## 2026-04-04 会话 013

### 实现功能

- 完成性能优化第 1 项：新增可开关背面剔除与运行时 MSAA 档位切换（1x/2x/4x）。
- 在主循环新增快捷键：`F2` 切换背面剔除，`F3` 循环切换 MSAA 档位，并实时输出当前状态。
- 完成性能优化第 2 项：将 Mary 从“每个三角形一个对象”改为“单 `MeshObject` 持有网格 + 按索引遍历渲染”，显著减少对象数量与每帧分发开销。
- 已按要求将第 3 项（模型矩阵缓存与片段写回收敛）加入 `TODO.md`，并写入分步实现思路与验收标准。
- 完成构建验证：CMake 构建成功（`mySoftRender.exe` 生成通过）。

### 修改文件

- include/MeshObject.h
- include/Rasterizer.h
- include/software_renderer.h
- src/Rasterizer.cpp
- src/software_renderer.cpp
- src/main.cpp
- TODO.md
- CONVERSATION_LOG.md

## 2026-04-04 会话 014

### 实现功能

- 修复背面剔除方向判定错误：将 `Rasterizer` 中正面条件从 `signedArea < 0` 更正为 `signedArea > 0`。
- 补充中文注释，解释 `edgeFunction` 符号与屏幕坐标系（Y 轴向下）对正反面判断的影响。
- 完成 CMake 构建验证，确认修复后工程可正常编译通过。
- 按约束将本次错误根因与解决方案写入 `Experience.md`。

### 修改文件

- src/Rasterizer.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-06 会话 015

### 实现功能

- 实现 FPS 相机 WASD 移动：将前向向量投影到水平面，保证移动不受相机俯仰影响，符合第一人称地面平移习惯。
- 在主循环每帧更新中接入 `SDL_GetKeyboardState`，使 W/A/S/D 按住即可持续移动。
- 按速度与帧间隔计算位移（`speed * deltaTime`），保证不同帧率下移动手感一致。
- 完成 Debug 构建验证，工程可编译通过。

### 修改文件

- src/main.cpp
- CONVERSATION_LOG.md

## 2026-04-06 会话 016

### 实现功能

- 修复输入冲突回归：将键盘输入统一为每帧 `SDL_PumpEvents + SDL_GetKeyboardState` 链路。
- 将 `F1/F2/F3/ESC` 从 `SDL_KEYDOWN` 事件触发改为边沿检测，避免在部分环境下出现按键无响应。
- 保留 `SDL_PollEvent` 仅处理系统窗口事件（如关闭窗口），使输入职责更清晰。
- 保持 WASD 持续状态移动并完成 Debug 构建验证，编译通过。
- 按约束将本次错误原因与修复方案写入 `Experience.md`。

### 修改文件

- src/main.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-06 会话 017

### 实现功能

- 修复键盘输入彻底无响应问题：移除 `scancode` 依赖，全面改用虚拟按键映射 `sym`。
- 在特定运行环境（部分中文输入法、远控环境）下，SDL 的物理扫描码 `scancode` 无法正常响应，导致之前的改动即使统一了链路依旧失效。
- 将 `SDL_SCANCODE_W` 与 `SDL_SCANCODE_F1` 等硬绑定全盘替换为 `SDLK_w` 与 `SDLK_F1` 等逻辑键位，恢复输入功能。
- 完成 Debug/Release 构建验证。

### 修改文件

- src/main.cpp
- CONVERSATION_LOG.md
- Experience.md

## 2026-04-06 会话 018

### 实现功能

- 新增 FPS 相机俯仰角与水平转向控制：在主循环接入 `SDL_MOUSEMOTION`，使用鼠标相对位移驱动 `yaw/pitch`。
- 新增俯仰角限制：将 pitch 约束在 `[-89°, +89°]`，防止抬头/低头过界引发翻转。
- 新增相机角度初始化：从当前相机朝向反解初始 yaw/pitch，保证开启鼠标视角时画面连续。
- 新增窗口焦点联动：失焦时释放鼠标并清空 WASD 状态，重新聚焦时自动恢复相对鼠标模式。
- 完成 Debug/Release 双构建验证，工程编译通过。

### 修改文件

- src/main.cpp
- CONVERSATION_LOG.md

## 2026-04-06 会话 019

### 实现功能

- 修复“相机水平转身 180 度后仍看到身后模型且翻转”的渲染错误。
- 在 `SoftwareRenderer` 投影入口新增裁剪空间保守拒绝：
	- 任一顶点 `clip.w <= epsilon` 直接拒绝。
	- 若三顶点同时落在任一裁剪平面外（`x/y/z` 相对 `±w`），整三角形拒绝。
- 将齐次除法严格放在裁剪检查之后，避免后方几何被 `clip/w` 镜像映射到前方。
- 完成 Debug/Release 双构建验证，编译通过。
- 按要求将本次错误根因与修复策略写入 `Experience.md`。

### 修改文件

- src/software_renderer.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-07 会话 020

### 实现功能

- 为 `Texture2D` 的各个函数补充中文注释，覆盖接口声明与实现函数。
- 注释内容统一包含“作用/用法”，便于快速理解纹理加载、棋盘纹理生成、texel 读取与双线性采样流程。
- 完成静态错误检查，确认注释改动未引入编译问题。

### 修改文件

- include/Texture.h
- src/Texture.cpp
- CONVERSATION_LOG.md

## 2026-04-07 会话 021

### 实现功能

- 基于当前项目真实进度，清理并重写 `TODO.md` 结构，按“已完成基线 / P0 / P1 / P2 / 工程化”分层。
- 修正过期项状态（例如纹理管线、FPS 相机、保守裁剪拒绝等已完成能力）。
- 新增独立的“光源系统（新增）”TODO，明确目标、拆分步骤与验收标准。
- 保留里程碑建议并更新为当前阶段可执行路径。

### 修改文件

- TODO.md
- CONVERSATION_LOG.md

## 2026-04-07 会话 022

### 实现功能

- 修复 `main.cpp` 中 `renderer.SetFragmentShader(Bling_Phong_Shader);` 的类型不匹配报错。
- 将 `Bling_Phong_Shader` 的参数从 `Fragment&` 改为 `const Fragment&`，与渲染器接口签名一致。
- 继续排查并修复链接问题：将 `src/Light.cpp` 加入 CMake 目标，并修正其头文件引用路径。
- 完成 Release 构建验证，确认工程可编译通过。
- 按约束将本次错误根因与解决方案写入 `Experience.md`。

### 修改文件

- src/main.cpp
- src/Light.cpp
- CMakeLists.txt
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-07 会话 023

### 实现功能

- 将 Blinn-Phong 光照计算从屏幕空间改为世界空间：`lightDir/viewDir` 改为基于 `payload.worldPos` 与相机世界位置计算。
- 在 `Fragment` 中新增 `worldPos` 字段，并在光栅化阶段按重心坐标插值世界位置与世界法线。
- 在 `SoftwareRenderer` 的三角形提交流程中补齐世界空间属性传递：
	- 由 `modelMatrix` 生成每顶点世界坐标。
	- 使用法线矩阵（逆转置）将局部法线变换到世界空间。
- 修复 shader 回调触发条件：移除无效的 `weak_ptr scene_` 依赖，改为直接使用 `DrawScene` 传入的 `scene`。
- 修复场景光源未生效问题：创建点光源后加入 `scene.lights` 列表。
- 完成 Debug/Release 双构建验证，工程编译通过。

### 修改文件

- include/Rasterizer.h
- src/Rasterizer.cpp
- include/software_renderer.h
- src/software_renderer.cpp
- src/main.cpp
- CONVERSATION_LOG.md

## 2026-04-07 会话 024

### 实现功能

- 修复“运行时看不出 Blinn-Phong 光照效果”的核心问题：材质、光照参数与着色器合成方式同步校正。
- 在场景创建阶段将球体基础颜色改为白色并绑定球体纹理，避免黑色反照率导致受光层次被吞没。
- 重写 `Bling_Phong_Shader` 合成逻辑：由“直接叠加 albedo”改为“`albedo * (ambient + diffuse) + specular`”的标准形式。
- 点光衰减由简单 `1/r^2` 改为更平滑的 `1/(1 + 0.14d + 0.07d^2)`，并调整光源位置与强度，提升默认场景下的可见高光与明暗对比。
- 调低场景环境光强度，避免环境光过亮抹平漫反射和高光细节。
- 完成 Release 构建验证，工程编译通过。

### 修改文件

- src/main.cpp
- Experience.md
- CONVERSATION_LOG.md


