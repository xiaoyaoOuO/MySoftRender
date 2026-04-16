# IBL 环境光照任务文档（仅任务规划，不改代码）

## 1. 目标与范围

目标：在现有 Blinn-Phong 渲染链路上实现 IBL（Image-Based Lighting）环境光照，使环境贡献不再是常量色，而是来源于天空盒的方向相关光照。

本任务分两阶段推进，避免一次改动过大。

阶段 A（MVP）：Diffuse IBL

- 保留现有常量环境光。
- 新增天空盒驱动的漫反射环境光（按法线方向采样）。
- 支持运行时开关与强度调节。
- 不改阴影流程。

阶段 B（增强）：Specular IBL（Split-Sum）

- 新增预过滤环境图（Prefiltered Env Map）。
- 新增 BRDF LUT 采样。
- 接入粗糙度相关高光环境反射。

本轮不做：

- 不改为完整 PBR 材质系统。
- 不做 SSR、Probe、AO、SH 多项工程化特性。
- 不重构 Rasterizer 和 Shadow Pass 主流程。

## 2. 当前代码基线（可复用能力）

现有能力与入口：

- 场景常量环境光字段：[include/Scene.h](include/Scene.h#L46), [include/Scene.h](include/Scene.h#L47)
- 场景天空盒资源字段：[include/Scene.h](include/Scene.h#L49), [include/Scene.h](include/Scene.h#L50)
- 片元着色入口：[src/main.cpp](src/main.cpp#L539)
- 当前常量环境光计算：[src/main.cpp](src/main.cpp#L547), [src/main.cpp](src/main.cpp#L563)
- 当前阴影合成规则（仅衰减直射）：[src/main.cpp](src/main.cpp#L619)
- 天空盒采样接口（方向 -> 颜色）：[src/Texture.cpp](src/Texture.cpp#L276)
- 天空盒背景绘制入口：[src/software_renderer.cpp](src/software_renderer.cpp#L641)
- 调试光照页签入口：[src/DebugUI.cpp](src/DebugUI.cpp#L252)

结论：

- Diffuse IBL 可直接在当前片元着色函数落地，无需改光栅化结构。
- Specular IBL 需要补充纹理侧能力（至少 LOD 或预过滤图资源接入）。

## 3. IBL 设计方案

## 3.1 阶段 A：Diffuse IBL（推荐先落地）

思路：

- 使用片元法线 N 在环境图中采样，得到环境漫反射近似颜色。
- 环境项不受 shadowVisibility 衰减。

公式建议：

- ambientConst = ambientLightColor * ambientLightIntensity
- ambientIblDiffuse = SampleIrradiance(N) * iblDiffuseIntensity
- final = albedo * (ambientConst + ambientIblDiffuse)
- final += shadowVisibility * directLighting
- final += emissive

说明：

- MVP 可先直接使用 scene.skyboxTexture 作为 irradiance 近似。
- 后续可切换为独立 irradiance cubemap。

## 3.2 阶段 B：Specular IBL（Split-Sum）

思路：

- 使用反射向量 R 采样预过滤环境图（按 roughness 选 LOD）。
- 使用 BRDF LUT（NdotV, roughness）近似菲涅耳与几何项积分。

公式建议：

- F0 = mix(vec3(0.04), albedo, metallic)
- prefiltered = PrefilterEnvSample(R, roughness)
- brdf = BRDFLUT(NdotV, roughness)
- specIbl = prefiltered * (F * brdf.x + brdf.y)
- ambient = kD * diffuseIbl + specIbl

说明：

- 当前项目未提供材质粗糙度/金属度，首版可从 Scene 全局参数给默认值。

## 4. 文件改动任务清单（按阶段）

## 4.1 阶段 A（Diffuse IBL）

文件 1：[include/Scene.h](include/Scene.h)

新增建议：

1. IBL 设置结构体

- 名称建议：IBLSettings
- 字段建议：
  - bool enableIBL = true
  - bool enableDiffuseIBL = true
  - bool enableSpecularIBL = false
  - float diffuseIntensity = 0.25f
  - float specularIntensity = 1.0f（阶段 A 可先保留）
  - float roughness = 0.5f（阶段 B 使用）
  - float metallic = 0.0f（阶段 B 使用）

2. Scene 中新增字段

- IBLSettings iblSettings;

可选新增（为阶段 B 提前预留）：

- std::shared_ptr<CubemapTexture> iblIrradianceMap;
- std::shared_ptr<CubemapTexture> iblPrefilterMap;
- std::shared_ptr<Texture2D> iblBrdfLut;

文件 2：[src/main.cpp](src/main.cpp)

目标函数：

- [src/main.cpp](src/main.cpp#L539)

需要补充的辅助函数（建议放在 main.cpp 匿名命名空间）：

1. ComputeConstantAmbientLighting

- 输入：Scene
- 输出：glm::vec3
- 说明：复用现有 ambientLightColor 与 ambientLightIntensity。

2. ComputeDiffuseIblLighting

- 输入：Scene, worldNormal
- 输出：glm::vec3
- 行为：
  - iblSettings.enableIBL / enableDiffuseIBL 任一为 false 返回 0。
  - 优先使用 iblIrradianceMap；若为空则回退 skyboxTexture。
  - worldNormal 安全归一化后采样环境图。
  - 乘以 diffuseIntensity。

3. ComputeAmbientLighting

- 输入：Scene, worldNormal
- 输出：glm::vec3
- 行为：constantAmbient + diffuseIBL。

BlingPhongShader 修改点：

- 用 ComputeAmbientLighting(scene, normal) 取代当前单一 ambientLighting。
- 保持当前阴影合成原则不变：阴影只衰减直射项。

文件 3：[src/main.cpp](src/main.cpp)

目标函数：

- ResetSceneState：[src/main.cpp](src/main.cpp#L384)

需要修改：

- 初始化 scene.iblSettings 默认值。
- 建议默认值：
  - enableIBL = true
  - enableDiffuseIBL = true
  - diffuseIntensity = 0.2~0.35

文件 4：[src/DebugUI.cpp](src/DebugUI.cpp)

目标函数：

- [src/DebugUI.cpp](src/DebugUI.cpp#L252)

UI 新增建议：

1. IBL 开关与强度

- Checkbox: Enable IBL
- Checkbox: Diffuse IBL
- DragFloat: Diffuse IBL Intensity

2. 现有常量环境光调节

- ColorEdit3: Ambient Color
- DragFloat: Ambient Intensity

3. 状态提示

- 显示 skyboxName
- 无可用环境图时显示回退提示

参数保护：

- intensity 下限钳制为 0。

## 4.2 阶段 B（Specular IBL）

文件 5：[include/Texture.h](include/Texture.h)

新增建议：

- 为 CubemapTexture 增加 LOD 采样接口（示例）：sampleLod(direction, lod)
- 或保留 sample 不变，改为直接加载独立预过滤 cubemap（推荐先走资源加载，改动更小）

文件 6：[src/Texture.cpp](src/Texture.cpp)

新增建议二选一：

方案 1（推荐，低风险）：

- 仅增加预过滤图与 irradiance 图的加载约定，不做运行时卷积。

方案 2（高成本）：

- 运行时从 skybox 生成 irradiance / prefilter（CPU 成本较高，可能影响启动时长）。

文件 7：[src/main.cpp](src/main.cpp)

新增辅助函数建议：

- FresnelSchlickRoughness
- ComputeSpecularIblLighting
- ComputeIblLighting（汇总 diffuse + specular）

BlingPhongShader 新增合成：

- 在 ambient 分量里叠加 specular IBL。

文件 8：[src/DebugUI.cpp](src/DebugUI.cpp)

新增调参：

- Checkbox: Specular IBL
- DragFloat: Specular IBL Intensity
- DragFloat: Roughness
- DragFloat: Metallic

## 5. 资源目录规范（建议）

建议与现有 skybox 目录并行：

- assets/cubemap/<SkyboxName>/（原六面图）
- assets/cubemap/<SkyboxName>/ibl/irradiance/（六面图）
- assets/cubemap/<SkyboxName>/ibl/prefilter/（六面图或多级目录）
- assets/cubemap/<SkyboxName>/ibl/brdf_lut.png

加载优先级建议：

1. 优先加载独立 IBL 资源（irradiance + prefilter + brdf）
2. 缺失时回退 Diffuse IBL（直接采样 skybox）
3. 再缺失回退常量环境光

## 6. 不改动项（本任务边界）

本任务不应修改：

- [src/software_renderer.cpp](src/software_renderer.cpp)
- [src/Rasterizer.cpp](src/Rasterizer.cpp)
- [include/Rasterizer.h](include/Rasterizer.h)

原因：

- IBL 主要属于片元着色与资源管理问题，当前片元输入已满足 MVP。

## 7. 验收标准

阶段 A 验收：

- 有 skybox 时：背光区域可见环境色调变化，不再只由常量环境光决定。
- 关闭 IBL 时：画面退回当前常量环境光表现。
- 阴影区域：环境光仍可见，直射项仍受 shadowVisibility 影响。

阶段 B 验收：

- 高光随观察角变化有环境反射特征。
- roughness 增大时，高光环境反射变宽变糊。
- 切换 skybox 后，IBL 漫反射与镜面反射均随环境变化。

工程验收：

- Debug 构建通过。
- Release 构建通过。
- 无明显帧率回退（阶段 A 基本不应有显著影响）。

## 8. 风险与规避

风险 1：IBL 过亮导致整体发灰。

- 规避：默认 diffuseIntensity 取低值，开放 UI 实时调节。

风险 2：法线异常导致采样闪烁。

- 规避：统一使用安全归一化，零向量回退固定方向。

风险 3：Specular IBL 资源不完整。

- 规避：明确三级回退链（完整 IBL -> Diffuse IBL -> 常量环境光）。

风险 4：运行时卷积带来启动卡顿。

- 规避：优先使用离线预计算资源加载，不在首版做 CPU 卷积。

## 9. 实施顺序（建议）

1. 落地 Scene 的 IBLSettings 字段（不改行为）
2. 完成阶段 A 的 shader 逻辑与 UI 开关
3. 完成阶段 A 构建与效果验收
4. 接入阶段 B 的资源字段与加载流程
5. 完成 Specular IBL 合成与 UI 调参
6. 做完整回归与性能检查

## 10. 本文档对应的实现里程碑

- M1：Diffuse IBL 可用（1~2 次提交）
- M2：Specular IBL 可用（2~4 次提交）
- M3：资源加载/回退链完整（1 次提交）
