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

## 2026-04-07 会话 025

### 实现功能

- 修复“已加载 floor 但场景中不可见”的问题。
- 根因定位：`floor.obj` 为超大平面（约 56x56），当前渲染器在投影前采用保守 reject（任一顶点在相机后方即丢弃整三角），导致 floor 的两个三角形都被整块剔除。
- 解决方式：在 `CreateScene` 中对 floor 增加缩放并前移，使其顶点整体落在相机前方，避免触发整三角拒绝。
- 完成 Release 构建验证，工程编译通过。

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

## 2026-04-11 会话 026

### 实现功能

- 开始实现 ShadowMap 主流程：在 `SoftwareRenderer::DrawScene` 中接入双 pass 渲染（方向光深度 pass + 主相机颜色 pass）。
- 扩展光源与场景阴影配置：
	- `Light` 新增光空间矩阵缓存与阴影体参数（正交半径、近平面、远平面）。
	- `Scene` 新增 `ShadowSettings`（开关、分辨率、过滤模式、PCF/PCSS 预留参数）。
- 片段数据新增阴影可见性：`Fragment` 增加 `shadowVisibility` 字段，并在主着色阶段应用到方向光直接光贡献。


### 修改文件

- include/Light.h
- src/Light.cpp
- include/Scene.h
- include/Rasterizer.h
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-11 会话 027

### 实现功能

- 按“先不改太多”的要求，完成 Light 第一个 Pass 的深度写入闭环。
- 修正原先第一 Pass 与主 Pass 共用 `Rasterizer` 深度缓冲的问题，改为独立阴影深度缓存流程，避免互相污染。
- 在 `SoftwareRenderer` 内新增第一 Pass 的最小深度光栅逻辑：
	- 新增目标投影函数 `ProjectTriangleToTarget`，将三角形投影到阴影贴图分辨率。
	- 新增深度写入函数 `RasterizeShadowDepthTriangle`，仅写最近深度。
	- 将深度结果写入 `Light` 的 `lightViewDepths` 缓存。
- 在 `Light` 中新增 `lightViewDepths()` 访问接口，供第一 Pass 写入、后续第二 Pass 读取。
- 修复一次编译错误：`rasterizeLocalTriangle` 默认参数在声明/定义重复，移除实现处默认值后构建通过。
- 完成 Debug 构建验证，工程编译通过。

### 修改文件

- include/Light.h
- src/software_renderer.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-11 会话 028

### 实现功能

- 按需求将 Light 的 depthbuffer 尺寸调整为与软件渲染 colorbuffer 完全一致。
- 第一 Pass 深度缓存分配从 `shadowMapResolution x shadowMapResolution` 改为 `width_ x height_`。
- 第一 Pass 的投影与深度光栅写入同步改为使用 `shadowWidth/shadowHeight`，保证索引和边界一致。
- 完成 Debug 构建验证，工程编译通过。

### 修改文件

- src/software_renderer.cpp
- CONVERSATION_LOG.md

## 2026-04-12 会话 029

### 实现功能

- 修复阴影随相机移动漂移问题：在投影阶段写入每顶点 `invW`，并在光栅化阶段对 `worldPos/worldNormal/UV` 使用透视校正重心插值。
- 修复阴影采样不稳定问题：为 ShadowMap 读取补齐 NDC 范围检查、贴图边界检查与尺寸一致性检查，消除越界读取导致的随机阴影形变。
- 补齐阴影贴图尺寸元数据：在 `Light` 中新增 shadow map 宽高字段，由第一 Pass 写入、第二 Pass 读取，避免采样维度隐式耦合。
- 修复阴影模型与光照模型不一致：将场景主光源改为方向光，并设置正交阴影范围参数，使当前 2D ShadowMap 路径与光照类型一致。
- 完成 Debug/Release 双构建验证，工程编译通过。
- 按约束将本次问题的根因与修复方案写入 `Experience.md`。
- 处理一次 release 链接占用问题：`mySoftRender.exe` 被运行进程锁定导致 `Permission denied`，终止进程后重建通过。

### 修改文件

- include/ObjLoader.h
- include/Light.h
- src/Light.cpp
- src/software_renderer.cpp
- src/Rasterizer.cpp
- src/main.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-12 会话 030

### 实现功能

- 按需求将主光源调整到人物前上方，并重设方向向量，使人物可在地面形成更完整、稳定的投影轮廓。
- 新增独立 ImGui 调试模块 `DebugUI`，将 UI 生命周期与渲染调用从 `main.cpp` 中解耦。
- 在调试面板中接入 `scene.shadowSettings.depthBias` 实时滑杆，运行时可直接调节阴影深度偏移。
- 增加交互快捷键：
	- `F4`：显示/隐藏调试面板
	- `F5`：切换鼠标捕获（便于在 FPS 视角与 ImGui 交互之间切换）
- 完成 Debug/Release 双构建验证，工程编译通过。

### 修改文件

- include/DebugUI.h
- src/DebugUI.cpp
- src/main.cpp
- CMakeLists.txt
- CONVERSATION_LOG.md

## 2026-04-12 会话 031

### 实现功能

- 在独立 `DebugUI` 模块中扩展调试面板，新增光源设置区：
	- 主光源位置、方向、颜色、强度
	- 投影参数（OrthoHalfSize / Near / Far）
	- 阴影开关（Cast Shadow）
- 新增模型移动区：支持选择场景对象并实时调整位置、旋转、缩放。
- 新增“光源移动到选中模型前上方”一键按钮，并自动朝向模型中心，便于快速获得完整投影效果。
- 保留并整合原有阴影参数调节（Enable ShadowMap / Depth Bias），继续通过 ImGui 实时生效。
- 完成 Debug/Release 双构建验证，工程编译通过。

### 修改文件

- include/DebugUI.h
- src/DebugUI.cpp
- CONVERSATION_LOG.md

## 2026-04-12 会话 032

### 实现功能

- 开始点光源阴影主流程实现（第一版可运行）：
	- 在 `Light` 中新增点光阴影 6 面矩阵与 6 面深度缓存接口。
	- 在渲染器 Shadow Pass 中新增点光 6 面深度写入路径（按 `shadowMapResolution` 分辨率逐面生成深度图）。
	- 在光栅化阶段新增点光阴影采样分支：按 `worldPos-lightPos` 选择采样面，并使用对应面矩阵进行深度比较。
- 新增对象级阴影投射开关 `Object::setCastShadow / castShadow`，用于排除调试辅助对象参与阴影投射。
- 场景接入点光源可视化代理：
	- 在 `CreateScene` 中加入点光源小球代理对象。
	- 每帧将代理对象位置同步到点光源位置，使相机可直接看到点光源。
- 调试面板增强：
	- 新增光源类型切换（Point/Directional/Spot）。
	- 新增阴影分辨率调节滑杆（`Shadow Resolution`）。
- 完成 Debug/Release 双构建验证，工程编译通过。

### 修改文件

- include/Object.h
- include/Scene.h
- include/Light.h
- src/Light.cpp
- src/software_renderer.cpp
- src/Rasterizer.cpp
- src/main.cpp
- src/DebugUI.cpp
- CONVERSATION_LOG.md

## 2026-04-12 会话 033

### 实现功能

- 完成“Start implementation”阶段收尾核对：逐文件定位并确认点光源阴影与点光可视化改动已落地。
- 补充关键代码定位，便于后续验收与继续迭代（Light/Renderer/Rasterizer/Main/DebugUI 关键入口）。
- 本次为说明与验收准备，不新增逻辑代码；沿用上一阶段已通过的 Debug/Release 构建结果。

### 修改文件

- CONVERSATION_LOG.md

## 2026-04-12 会话 034

### 实现功能

- 根据最新 `Restrict.md` 继续执行“减少函数内 Lambda”的重构：
	- 将 `software_renderer.cpp` 中裁剪平面判断与 ShadowPass 对象光栅化逻辑提取为具名函数。
	- 将 `main.cpp` 中鼠标捕获控制与片元着色入口改为具名函数（不再使用函数内 Lambda）。
	- 将 `Rasterizer.cpp` 中线框片元写入与像素着色回调改为具名函数/具名可调用对象。
	- 将 `ObjLoader.cpp` 中顶点索引查找创建逻辑从 Lambda 提取为 `GetOrCreateVertexIndex`。
- 按新规范统一函数注释样式为 Doxygen 形式（`@brief/@param/@return`），应用到本轮新增与重构函数。
- 完成静态错误检查与 Debug 构建验证，编译通过。

### 修改文件

- src/software_renderer.cpp
- src/main.cpp
- src/Rasterizer.cpp
- src/ObjLoader.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-12 会话 035

### 实现功能

- 按需求仅提供 PCF/PCSS 实现文档，不改动渲染逻辑代码。
- 文档明确给出：
	- 当前阴影调用链与接入位置；
	- PCF/PCSS 的实现思路与分阶段落地策略；
	- 建议新增函数清单（名称、职责、参数建议）；
	- 具体调用点（重点在 `Rasterizer` 阴影可见性分支）与 `DebugUI` 参数接入点。
- 文档同时覆盖 Directional 与 Point 两条阴影采样路径，以及 PCSS blocker search / penumbra / 可变核过滤三阶段。

### 修改文件

- PCF_PCSS_IMPLEMENTATION.md
- CONVERSATION_LOG.md

## 2026-04-12 会话 036

### 实现功能

- 按需求对 `RasterizeTriangleMSAA` 做“部分函数提取”，减少 if 嵌套层级。
- 将原先内联在光栅主循环中的阴影判定深层分支拆分为具名函数：
	- `ProjectWorldPosToShadowTexel`
	- `ComputeHardShadowVisibilityFromDepthMap`
	- `ComputePointShadowVisibility`
	- `ComputeDirectionalShadowVisibility`
	- `ComputeShadowVisibility`
- 主流程内改为单行调用 `frag.shadowVisibility = ComputeShadowVisibility(...)`，显著降低分支深度并保持原行为。
- 完成语法检查与 Debug 构建验证，编译通过。

### 修改文件

- src/Rasterizer.cpp
- CONVERSATION_LOG.md

## 2026-04-12 会话 037

### 实现功能

- 为 `ShadowSettings` 补全对应的 DebugUI 控件接入（位于 Shadow Settings 面板）：
	- 新增 `Normal Bias` 滑杆。
	- 新增 `Filter Mode`（Hard / PCF / PCSS）切换。
	- 新增 PCF 参数控件：`PCF Kernel Radius`、`PCF Sample Count`。
	- 新增 PCSS 参数控件：`PCSS Blocker Samples`、`PCSS Filter Samples`、`PCSS Light Size`。
- 增加参数下限保护，防止运行时出现非法值（负 bias、0 采样等）。
- 完成静态错误检查与 Debug 构建验证，编译通过。

### 修改文件

- src/DebugUI.cpp
- CONVERSATION_LOG.md

## 2026-04-12 会话 038

### 实现功能

- 修复 `src/Rasterizer.cpp` 中阴影修复补丁误落位导致的语法/作用域损坏，恢复线框光栅函数结构。
- 在 `ComputeShadowVisibilityFromDepthMap` 内落实 PCF 边界采样修复：
	- 增加阴影贴图尺寸保护（宽高与缓存长度）。
	- PCF 核采样使用边界钳制，保持分子/分母统计一致。
	- 阴影可见性结果统一钳制到 `[0,1]`。
- 完成构建验证：`cmake build` 与 `cmake build release` 均编译通过。
- 按规范补充故障复盘记录到 `Experience.md`。

### 修改文件

- src/Rasterizer.cpp
- Experience.md
- CONVERSATION_LOG.md

## 2026-04-12 会话 039

### 实现功能

- 按最新注释规范，完成全项目 `src/` 与 `include/` 下 `// 作用：`、`// 用法：` 风格注释的彻底清理。
- 对成对出现的“作用/用法”注释统一合并为一行中文说明，改为函数体内可直接阅读的单行解释。
- 对未成对出现的“作用”或“用法”注释，统一改写为普通中文说明注释，移除旧标签前缀。
- 完成全量复查，确认代码文件中已无 `// 作用` 或 `// 用法` 残留。
- 完成 Debug 构建验证，工程编译通过。

### 修改文件

- src/main.cpp
- src/Light.cpp
- src/DebugUI.cpp
- src/Rasterizer.cpp
- src/software_renderer.cpp
- src/Texture.cpp
- include/DebugUI.h
- include/Light.h
- include/MeshObject.h
- include/Object.h
- include/ObjLoader.h
- include/Rasterizer.h
- include/Scene.h
- include/software_renderer.h
- include/Texture.h
- CONVERSATION_LOG.md

## 2026-04-13 会话 040

### 实现功能

- 按“仅做模块、不接入渲染调用”的要求，新增独立线程池模块 `RenderThreadPool`。
- 线程池能力包含：
	- 任务提交 `enqueue`（返回 `future`）。
	- 区间并行 `parallelFor(begin, end, chunk)`。
	- 空闲同步 `waitIdle`、状态查询 `idle/pendingTaskCount`。
	- 运行期线程数调整 `setThreadCount`。
- 新模块已加入 CMake 编译目标，但未在渲染流程中调用。
- 完成构建验证：`mySoftRender` 编译通过。

### 修改文件

- include/RenderThreadPool.h
- src/RenderThreadPool.cpp
- CMakeLists.txt
- CONVERSATION_LOG.md

## 2026-04-14 会话 041

### 实现功能

- 说明并示范线程池使用方式：将 `SoftwareRenderer::DrawScene` 的 fragment shading 阶段改为使用 `RenderThreadPool::parallelFor` 分块并行执行。
- 新增具名上下文 `FragmentShadingContext` 与具名任务函数 `ShadeFragmentRange`，避免在函数内部继续扩展匿名 Lambda。
- 渲染器内部移除“每帧创建/回收线程”的片元并行路径，改为复用 `fragmentShadingThreadPool_` 常驻工作线程。
- 保持其余渲染流程（Shadow Pass、几何光栅化）不变，仅将片元阶段切换为线程池示范。
- 完成构建验证：`mySoftRender` 编译通过。

### 修改文件

- include/software_renderer.h
- src/software_renderer.cpp
- CONVERSATION_LOG.md

## 2026-04-14 会话 042

### 实现功能

- 按要求为线程池实现代码补充中文注释，覆盖构造/析构、线程启停、任务调度、并行分块与空闲同步等核心函数。
- 在 `RenderThreadPool.cpp` 中为关键并发步骤增加一行解释注释（任务入队、唤醒、异常汇总、活跃线程计数、空闲通知等）。
- 在 `RenderThreadPool.h` 的 `enqueue` 内联实现中补充关键流程注释，说明 `bind`、`packaged_task`、`future` 与任务唤醒的作用。
- 完成构建验证：`mySoftRender` 编译通过。

### 修改文件

- include/RenderThreadPool.h
- src/RenderThreadPool.cpp
- CONVERSATION_LOG.md

## 2026-04-14 会话 043

### 实现功能

- 在 DebugUI 中新增 Thread Pool 面板，显示片元线程池关键指标：
	- Pool Threads（线程池配置线程数）
	- Active Workers / Pending Tasks（当前活跃线程数与排队任务数）
	- Dispatched Workers / Scheduled Tasks / Fragments（本帧实际调度线程数、任务数与片元数）
- 新增 `Enable Fragment Multithreading` 开关，可在运行时切换片元着色多线程/单线程，便于效率对比。
- 在 `SoftwareRenderer` 中新增片元多线程开关与统计数据结构，并在 `DrawScene` 的 fragment shading 阶段实时更新统计。
- 在 `RenderThreadPool` 中新增 `activeWorkerCount()` 接口，用于 UI 展示实际运行线程数。
- 更新主循环 UI 调用签名，向 `DebugUI::drawShadowPanel` 传入 `SoftwareRenderer`。
- 完成构建验证：`mySoftRender` 编译通过。

### 修改文件

- include/RenderThreadPool.h
- src/RenderThreadPool.cpp
- include/software_renderer.h
- src/software_renderer.cpp
- include/DebugUI.h
- src/DebugUI.cpp
- src/main.cpp
- CONVERSATION_LOG.md

## 2026-04-14 会话 044

### 实现功能

- 完成 DebugUI 布局优化：将原单列折叠面板重构为顶部 Scene 区 + Tab 分组（Shadow / Light / Model / Threading / Status），减少纵向滚动并提升高频操作可达性。
- 在 DebugUI 中新增场景切换下拉框（Scene 1 / Scene 2），并实现“请求-消费”模式：
	- UI 仅发起切换请求；
	- 主循环在安全点执行场景重建，避免 UI 层直接管理 Scene 生命周期。
- 在 `main.cpp` 中新增双场景预设构建：
	- Scene 1：Mary 模型 + 地板 + 点光源（保持原有场景）；
	- Scene 2：两个球体 + 地板 + 中上方点光源。
- 新增统一场景构建入口 `BuildSceneByPreset` 与场景资源结构 `SceneBuildResources`，将场景切换逻辑从渲染循环中解耦。
- 场景切换后重置相机姿态同步（重新提取并应用 yaw/pitch）并清空 WASD 连续输入状态，确保切换后视角稳定。
- 完成 Debug 构建验证，`mySoftRender` 编译通过。

### 修改文件

- include/DebugUI.h
- src/DebugUI.cpp
- src/main.cpp
- CONVERSATION_LOG.md

## 2026-04-14 会话 045

### 实现功能

- 修复“点光源可视化球体发黑”问题：调整 `BlingPhongShader` 光照合成逻辑。
- 将阴影可见性从“衰减全部光照”改为“仅衰减直射项”，避免环境光被阴影完全压黑。
- 为点光源近场片元增加灯芯自发光项（`kLightCoreRadius`），使光源可视化球体在光源中心附近始终保持发亮效果。
- 完成 Debug 构建验证，`mySoftRender` 编译通过。

### 修改文件

- src/main.cpp
- CONVERSATION_LOG.md


