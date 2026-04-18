# 对话实现记录（压缩版）

用途：保留每次会话的**核心结果**与**关键改动位置**，便于快速回顾。  
记录规则：每个会话只写「做了什么」+「改了哪些模块」，详细技术细节转入 `Experience.md`。

---

## 2026-04-01

- **会话 001**：完成纹理系统首轮接入（Texture2D、UV 光栅化、球体贴图回退）。关键文件：`Texture.*` `Rasterizer.*` `Sphere.*` `software_renderer.*` `main.cpp` `CMakeLists.txt`。
- **会话 002**：修复纹理路径在不同工作目录下查找失败，启动日志输出命中路径。关键文件：`src/main.cpp`。
- **会话 003**：MSAA 重写为 sample 独立深度/颜色缓存（4x）。关键文件：`Rasterizer.*`。
- **会话 004**：MSAA 调整为“sample 级覆盖/深度 + 像素级单次着色”传统路径。关键文件：`src/Rasterizer.cpp`。
- **会话 005**：新增独立 OBJ 加载模块（v/vt/vn/f、负索引、三角化、法线补全）。关键文件：`ObjLoader.*` `CMakeLists.txt`。
- **会话 006**：为 ObjLoader 增补中文注释。关键文件：`ObjLoader.*`。

## 2026-04-04

- **会话 007**：补充 MSAA 代码注释，不改算法。关键文件：`src/Rasterizer.cpp`。
- **会话 008**：修复 Debug 构建 `collect2/ld` 链接失败，统一 VSCode 任务 MinGW 环境。关键文件：`.vscode/tasks.json`。
- **会话 009**：纹理归属从 renderer 下沉到 object，实现对象独立贴图；同时补 `Triangle.h` 头文件依赖。关键文件：`Object.h` `Triangle.h` `software_renderer.*` `main.cpp`。
- **会话 010**：新建 `Experience.md` 复盘文档。关键文件：`Experience.md`。
- **会话 011**：修复 OBJ 贴图上下颠倒与 UV 接缝误判。关键文件：`ObjLoader.cpp` `Rasterizer.cpp`。
- **会话 012**：完成 OBJ 渲染性能瓶颈分析与优化优先级整理。关键文件：`Experience.md` `CONVERSATION_LOG.md`。
- **会话 013**：上线背面剔除开关 + MSAA 档位切换；Mary 改为单 MeshObject 渲染路径。关键文件：`MeshObject.h` `Rasterizer.*` `software_renderer.*` `main.cpp` `TODO.md`。
- **会话 014**：修复背面剔除符号方向（`signedArea > 0`），同步写入经验库。关键文件：`Rasterizer.cpp` `Experience.md`。

## 2026-04-06

- **会话 015**：实现 FPS 相机 WASD 持续移动（速度按 `deltaTime`）。关键文件：`main.cpp`。
- **会话 016**：输入链路统一为 `PumpEvents + KeyboardState`，功能键改边沿检测。关键文件：`main.cpp` `Experience.md`。
- **会话 017**：输入兼容性修复：由 `scancode` 迁移到 `keysym.sym/SDLK_*`。关键文件：`main.cpp` `Experience.md`。
- **会话 018**：接入鼠标视角 yaw/pitch、俯仰限制、焦点联动。关键文件：`main.cpp`。
- **会话 019**：修复“转身 180° 仍看到身后物体”问题，恢复 clip-space 保守 reject。关键文件：`software_renderer.cpp` `Experience.md`。

## 2026-04-07

- **会话 020**：补充 `Texture2D` 注释。关键文件：`Texture.h` `Texture.cpp`。
- **会话 021**：重写 `TODO.md`（按优先级/里程碑分层）。关键文件：`TODO.md`。
- **会话 022**：修复 shader 签名不匹配 + `Light.cpp` 未入 CMake 的链接问题。关键文件：`main.cpp` `Light.cpp` `CMakeLists.txt` `Experience.md`。
- **会话 023**：Blinn-Phong 切到世界空间（`worldPos/worldNormal`）；修复光源未生效链路。关键文件：`Rasterizer.*` `software_renderer.*` `main.cpp`。
- **会话 024**：修复“光照不明显”参数与合成公式问题。关键文件：`main.cpp` `Experience.md`。
- **会话 025**：修复 floor 已加载但不可见（缩放+位移避开保守 reject）。关键文件：`main.cpp` `Experience.md`。

## 2026-04-11

- **会话 026**：启动 ShadowMap 双 pass 主流程（方向光深度 pass + 相机 pass），扩展 Light/Scene/Fragment 阴影字段。关键文件：`Light.*` `Scene.h` `Rasterizer.h`。
- **会话 027**：完成第一 pass 深度写入闭环，Light 暴露深度缓存接口。关键文件：`Light.h` `software_renderer.cpp` `Experience.md`。
- **会话 028**：调整第一 pass 深度缓存尺寸与 color buffer 一致。关键文件：`software_renderer.cpp`。

## 2026-04-12

- **会话 029**：修复阴影漂移与形变（透视校正 + 阴影采样边界保护 + 主光改方向光）；处理 Release 进程占用。关键文件：`ObjLoader.h` `Light.*` `software_renderer.cpp` `Rasterizer.cpp` `main.cpp` `Experience.md`。
- **会话 030**：接入独立 `DebugUI` 模块；新增阴影偏移滑杆与 `F4/F5` 交互。关键文件：`DebugUI.*` `main.cpp` `CMakeLists.txt`。
- **会话 031**：DebugUI 增加光源/模型调参与“光源移到模型前上方”快捷操作。关键文件：`DebugUI.*`。
- **会话 032**：点光阴影首版可运行（6 面深度 pass + 采样分支），新增对象投影开关与点光代理球。关键文件：`Object.h` `Scene.h` `Light.*` `software_renderer.cpp` `Rasterizer.cpp` `main.cpp` `DebugUI.cpp`。
- **会话 033**：点光阴影阶段收尾核对（无新增逻辑）。关键文件：`CONVERSATION_LOG.md`。
- **会话 034**：按限制减少函数内 Lambda，提取具名函数并统一 Doxygen 注释。关键文件：`software_renderer.cpp` `main.cpp` `Rasterizer.cpp` `ObjLoader.cpp` `Experience.md`。
- **会话 035**：仅输出 PCF/PCSS 实施文档，不改代码。关键文件：`PCF_PCSS_IMPLEMENTATION.md`。
- **会话 036**：提取阴影可见性相关函数，降低 `RasterizeTriangleMSAA` 嵌套深度。关键文件：`Rasterizer.cpp`。
- **会话 037**：DebugUI 补齐 `ShadowSettings` 全套控件（Hard/PCF/PCSS + 参数保护）。关键文件：`DebugUI.cpp`。
- **会话 038**：修复阴影补丁误落位导致的 `Rasterizer` 结构损坏；落地 PCF 边界修复。关键文件：`Rasterizer.cpp` `Experience.md`。
- **会话 039**：全项目清理旧式“作用/用法”注释标签。关键文件：`src/*` `include/*`。

## 2026-04-13

- **会话 040**：新增独立线程池模块 `RenderThreadPool`（enqueue/parallelFor/waitIdle/可调线程数），暂不接入渲染流程。关键文件：`RenderThreadPool.*` `CMakeLists.txt`。

## 2026-04-14

- **会话 041**：示范将片元着色切到线程池并行（常驻线程池）。关键文件：`software_renderer.*`。
- **会话 042**：线程池模块补充中文注释。关键文件：`RenderThreadPool.*`。
- **会话 043**：DebugUI 新增 Thread Pool 面板与片元多线程开关，渲染器输出并行统计。关键文件：`RenderThreadPool.*` `software_renderer.*` `DebugUI.*` `main.cpp`。
- **会话 044**：DebugUI 重构为 Scene 区 + Tab；新增场景切换请求-消费机制与双场景预设。关键文件：`DebugUI.*` `main.cpp`。
- **会话 045**：修复点光代理球发黑，阴影仅衰减直射项并增加灯芯自发光。关键文件：`main.cpp`。

## 2026-04-16

- **会话 046**：接入 `CubemapTexture`、天空盒资源发现、Scene 天空盒状态与背景绘制路径。关键文件：`Texture.*` `Scene.h` `software_renderer.*` `DebugUI.*` `main.cpp` `Experience.md`。
- **会话 047**：天空盒性能优化（视线缓存 + 基于 zBuffer 的背景填充）。关键文件：`Rasterizer.h` `software_renderer.*`。
- **会话 048**：并行优化：天空盒背景并行 + 点光 6 面阴影并行。关键文件：`software_renderer.cpp`。
- **会话 049**：`drawShadowPanel` 拆分为 6 个子模块函数。关键文件：`DebugUI.*`。
- **会话 050**：`main.cpp` 按职责重构（输入/帧状态/事件/切换/渲染分层）。关键文件：`main.cpp`。
- **会话 051**：将环境光任务文档升级为 IBL 分阶段方案（仅文档）。关键文件：`AMBIENT_SHADING_TASK.md`。
- **会话 052**：落地 IBL 阶段 A（Diffuse IBL），接入 Scene 参数、Shader 合成与 DebugUI 控件。关键文件：`Scene.h` `main.cpp` `DebugUI.cpp`。
- **会话 053**：Skybox/IBL 控件从 Light 页签拆到独立 Skybox 页签。关键文件：`DebugUI.*`。
- **会话 054**：调整场景预设语义：Scene1 关闭天空盒；Scene2 单球体无光源启用天空盒。关键文件：`main.cpp` `DebugUI.cpp`。
- **会话 055**：日志维护规则固定为“只在文件尾追加”，并写入经验库。关键文件：`Experience.md`。
- **会话 056**：落地 IBL 阶段 B（Specular IBL：prefilter + BRDF LUT + `sampleLod`）。关键文件：`Scene.h` `Texture.*` `main.cpp` `DebugUI.cpp`。
- **会话 057**：新增 `utility/SkyboxLutGenerator` 独立工具模块（不接入主流程）。关键文件：`utility/SkyboxLutGenerator.*`。
- **会话 058**：重写 README（渲染能力、构建方式、资源规范、LUT 使用）。关键文件：`README.md`。
- **会话 059**：新增离线命令行工具 `skyboxLutBaker` 并接入 CMake/README。关键文件：`utility/SkyboxLutBakerMain.cpp` `CMakeLists.txt` `README.md`。

## 2026-04-17

- **会话 060**：Diffuse IBL 回退路径改为模糊采样，新增 `diffuseBlurLod` 调参；同步修复 IBL 字段与默认参数构建错误。关键文件：`Scene.h` `software_renderer.h` `main.cpp` `DebugUI.cpp` `Experience.md`。
- **会话 061**：移除全局 `roughness/metallic`，改为对象级材质参数并接入 Model 面板。关键文件：`Scene.h` `DebugUI.cpp`。

## 2026-04-18

- **会话 062**：新增 Diffuse 五级离线卷积任务文档。关键文件：`DIFFUSE_CONVOLUTION_LOD_TASK.md`。
- **会话 063**：落地 `skyboxDiffuseLodBaker` 与运行时五级 Diffuse LOD 采样/回退链；扩展 DebugUI 与 README；批量生成现有天空盒 `_cov` 资源。关键文件：`CMakeLists.txt` `Scene.h` `main.cpp` `DebugUI.cpp` `stb_image_impl.cpp` `utility/SkyboxDiffuseLodBakerMain.cpp` `README.md`。
- **会话 064**：新增 Specular IBL 实施任务文档（按当前项目状态拆分阻塞修复、资源闭环、UI可观测与验收标准），用于后续直接执行。关键文件：`SPECULAR_IBL_TASK.md`。
- **会话 065**：按任务文档落地 Specular IBL 运行时链路：修复 `ComputeSpecularIblLighting` 回退链（Specular LOD -> prefilter -> skybox -> black）、补全 `CookTorrance_Shader` 写回并改为默认着色器；同步修复 DebugUI 的 IBL 字段失配并新增 Specular LOD 命中状态显示；更新 README 资源与回退说明。关键文件：`main.cpp` `DebugUI.cpp` `README.md` `Experience.md`。
- **会话 066**：批量为 `assets/cubemap` 下全部天空盒生成对应 LUT，输出到各目录 `ibl/skybox_lut.ppm`（128x128，samples=64），并完成结果校验。关键文件：`assets/cubemap/*/ibl/skybox_lut.ppm` `CONVERSATION_LOG.md`。
- **会话 067**：为 `skyboxLutBaker` 增加 PNG 导出能力（按 `--output` 扩展名自动选择 PNG/PPM），并批量为全部天空盒生成 `ibl/skybox_lut.png`，用于更直观的调试查看。关键文件：`utility/SkyboxLutGenerator.h` `utility/SkyboxLutGenerator.cpp` `utility/SkyboxLutBakerMain.cpp` `assets/cubemap/*/ibl/skybox_lut.png` `CONVERSATION_LOG.md`。
- **会话 068**：修复 BRDF LUT 命名匹配导致的 LUT miss：`LoadOptionalBrdfLut` 优先按每个天空盒名匹配 LUT（如 `<skybox>_lut.*`），并兼容 `skybox_lut.*` 与历史 `brdf_*` 命名；同时调整默认场景开关：场景一默认关闭 Skybox 与 IBL，场景二默认开启 Specular IBL 便于调试。关键文件：`src/main.cpp` `CONVERSATION_LOG.md`。
- **会话 069**：修复 roughness/metallic 对高光影响过小的问题：移除 `ComputeIblLighting` 调试残留返回，恢复正式能量合成；修正 Specular LOD 仅从 `ibl/specular_lod` 与 `ibl/prefilter_lod` 读取，不再误用 `_cov/diffuse_lod`；补齐 BRDF LUT 与 Specular 计算输入钳制/归一化。关键文件：`src/main.cpp` `Experience.md`。
- **会话 070**：按新约定将 Diffuse LOD 命名切换为 Specular LOD（`diffuseLodMode/diffuseManualLod` -> `specularLodMode/specularManualLod`），并将 cubemap 的 `_cov/lod0~lod4` 作为 Specular 高光采样源优先读取；同步更新 DebugUI 的 LOD 控件文案与字段绑定。关键文件：`include/Scene.h` `src/main.cpp` `src/DebugUI.cpp` `CONVERSATION_LOG.md`。
- **会话 071**：调整 `skyboxDiffuseLodBaker` 卷积等级为六级：`lod0` 无模糊、`lod1~lod5` 逐级增强，并将默认参数更新为 `samples=1,64,96,128,160,192`、`angles=0,10,18,30,45,62`；随后批量执行离线烘焙覆盖 `assets/cubemap/*/*_cov/lod*`，全部天空盒生成成功并产出 `lod5`。关键文件：`utility/SkyboxDiffuseLodBakerMain.cpp` `assets/cubemap/*/*_cov/lod5/*` `CONVERSATION_LOG.md`。
- **会话 072**：将离线卷积烘焙器命名统一为 Specular 语义：文件重命名为 `SkyboxSpecularLodBakerMain.cpp`，工具内 `Diffuse` 相关标识统一替换为 `Specular`，并同步 CMake 目标名 `skyboxSpecularLodBaker` 与 README 构建/使用示例。关键文件：`utility/SkyboxSpecularLodBakerMain.cpp` `CMakeLists.txt` `README.md` `CONVERSATION_LOG.md`。
- **会话 073**：修复 IBL Specular 材质参数失效：`Rasterizer::Rasterize_Triangle` 调用 `RasterizeTriangleMSAA` 时补传 `material`，避免片元材质丢失导致 `roughness/metallic` 始终回退默认值；Debug 构建验证通过。关键文件：`src/Rasterizer.cpp` `CONVERSATION_LOG.md` `Experience.md`。
- **会话 074**：对 Specular IBL 实现做代码审查并修复关键逻辑错位：统一 6 级 LOD 边界（UI/预览/手动档位全改为 `0~5`）、修复 `InitializeSkyboxSelection` 的 fallback 数组缺项、修复 `BuildScenePresetTwo` 材质创建内存泄漏、将 `ComputeIblLighting` 默认粗糙度改为 0.5 并修正能量合成（`albedo*kD*diffuse + specular`），同时在 `CookTorrance_Shader` 恢复直射光与阴影合成避免场景回归；构建验证通过。关键文件：`include/Material.h` `src/main.cpp` `src/DebugUI.cpp` `CONVERSATION_LOG.md`。
- **会话 075**：按“仅修改 LUT 生成”要求，将 `skyboxLutBaker` 的 `SkyboxLutGenerator` 从环境颜色模糊采样改为传统 Split-Sum BRDF 积分 LUT（R=scale，G=bias，B=0），并保持原输出文件路径不变；随后批量执行离线烘焙，覆盖 `assets/cubemap/*/ibl/skybox_lut.png` 与 `assets/cubemap/*/ibl/skybox_lut.ppm` 全部 8 个文件。关键文件：`utility/SkyboxLutGenerator.cpp` `utility/SkyboxLutGenerator.h` `utility/SkyboxLutBakerMain.cpp` `assets/cubemap/*/ibl/skybox_lut.png` `assets/cubemap/*/ibl/skybox_lut.ppm` `CONVERSATION_LOG.md`。
- **会话 076**：修复 BRDF LUT 使用中的黑边问题：将运行时 BRDF LUT 采样坐标修正为 `sample(roughness, NdotV)`（与离线生成轴一致），并将离线积分几何项切换到 IBL 公式 `k = roughness^2 / 2`；随后重新构建并批量覆盖 `assets/cubemap/*/ibl/skybox_lut.png/.ppm`。同时校验新 LUT：蓝通道范围 `B=[0,0]`、CornellBox 的纯黑像素数为 0。关键文件：`src/main.cpp` `utility/SkyboxLutGenerator.cpp` `assets/cubemap/*/ibl/skybox_lut.png` `assets/cubemap/*/ibl/skybox_lut.ppm` `CONVERSATION_LOG.md`。
- **会话 077**：根据当前实现重写 `TODO.md` 与 `README.md`：清理历史冗余与过时表述，统一 IBL/Specular LOD 六级命名、离线工具说明、构建运行入口与资源回退链，文档结构改为“现状基线 + 可执行任务”。关键文件：`TODO.md` `README.md` `CONVERSATION_LOG.md`。

---

## 快速检索提示

1. 按会话号检索：`会话 0xx`。  
2. 按功能检索：`MSAA` / `ShadowMap` / `Skybox` / `IBL` / `ThreadPool` / `DebugUI`。  
3. 深入问题复盘请看：`Experience.md`（包含根因与预防措施）。
