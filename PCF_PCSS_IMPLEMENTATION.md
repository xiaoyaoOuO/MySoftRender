# PCF + PCSS 实现文档（仅方案）

## 1. 目标

在现有阴影流程上补充两种过滤模式：

- PCF：固定核软阴影
- PCSS：接触硬、远处软的可变半影阴影

当前工程已经有模式和参数预留：

- [include/Scene.h](include/Scene.h)
  - ShadowFilterMode: Hard / PCF / PCSS
  - ShadowSettings: pcfKernelRadius、pcfSampleCount、pcssBlockerSearchSamples、pcssFilterSamples、pcssLightSize


## 2. 当前调用链（现状）

1. Shadow Pass（生成深度图）

- 位置：[src/software_renderer.cpp](src/software_renderer.cpp)
- 入口：SoftwareRenderer::DrawScene
- 说明：
  - Point 光：6 面深度图（pointLightViewDepths）
  - Directional/Spot 路径：单张 2D 深度图（lightViewDepths）

2. Camera Pass（做阴影判定）

- 位置：[src/Rasterizer.cpp](src/Rasterizer.cpp)
- 入口：RasterizeTriangleMSAA 内部阴影分支
- 现状：单次深度比较（Hard Shadow）直接写 frag.shadowVisibility


## 3. 设计原则

- 不改 Shadow Pass 输出格式（第一阶段）
  - 先基于现有深度图实现 PCF/PCSS，降低接入风险。
- 把阴影可见性计算集中为一个入口函数
  - 避免 RasterizeTriangleMSAA 内部出现大段模式分支。
- Directional 与 Point 分开实现
  - 采样坐标、深度来源、边界处理不同。


## 4. 需要新增的函数（建议）

建议都放在 [src/Rasterizer.cpp](src/Rasterizer.cpp) 的匿名命名空间中。

### 4.1 统一入口与模式分发

1. float EvaluateShadowVisibility(...)

- 职责：根据 scene.shadowSettings.filterMode 分发到 Hard / PCF / PCSS。
- 输入建议：
  - const Scene& scene
  - const Light& light
  - const glm::vec3& worldPos
  - const glm::vec3& normal
  - float baseBias
- 输出：0~1 可见性。

2. float EvaluateHardShadowVisibility(...)

- 职责：保留当前单次比较逻辑（Directional 与 Point 共用入口，内部可分支）。


### 4.2 PCF 相关

3. float EvaluatePCFShadowVisibilityDirectional(...)

- 职责：Directional/Spot 的 2D 阴影图 PCF。
- 关键步骤：
  - worldPos -> lightSpace -> uv/depth
  - 以 texelSize 做核采样
  - 比较 receiverDepth 与 sampledDepth，求平均

4. float EvaluatePCFShadowVisibilityPoint(...)

- 职责：Point 6 面阴影图 PCF。
- 关键步骤：
  - SelectPointShadowFace(lightToFragment)
  - faceMatrix 投影得到当前面 uv/depth
  - 在当前面做邻域采样（边界外样本可跳过或视为可见）

5. std::vector<Vec2> GetShadowSamplePattern(int sampleCount)

- 职责：返回固定采样模式（建议 Poisson Disk）。
- 说明：优先固定表，避免每帧随机导致闪烁。


### 4.3 PCSS 相关

6. bool FindAverageBlockerDepthDirectional(..., float& outAvgBlockerDepth)

- 职责：Blocker Search 阶段，找到遮挡者平均深度。
- 判定：sampleDepth < receiverDepth - bias 视为 blocker。

7. bool FindAverageBlockerDepthPoint(..., float& outAvgBlockerDepth)

- 职责：Point 光版本的 blocker 搜索。

8. float EstimatePenumbraRadiusTexels(...)

- 职责：根据 receiverDepth、avgBlockerDepth、pcssLightSize 计算滤波半径（texel）。
- 公式建议：
  - penumbra = (receiverDepth - avgBlockerDepth) / max(avgBlockerDepth, eps) * lightSize
  - radiusTexels = penumbra * shadowResolution

9. float EvaluatePCSSShadowVisibilityDirectional(...)

- 职责：Directional/Spot PCSS 三阶段：
  - blocker 搜索 -> 半影估计 -> 可变核 PCF

10. float EvaluatePCSSShadowVisibilityPoint(...)

- 职责：Point 光 PCSS 三阶段。


### 4.4 深度与偏移辅助（可选）

11. float LinearizeDepth01(float depth01, float nearPlane, float farPlane)

- 职责：将 0~1 深度线性化（PCSS 半影估计更稳定）。
- 说明：第一版可先不线性化，后续再加。

12. float ComputeReceiverBias(const glm::vec3& normal, const glm::vec3& lightDir, const ShadowSettings& settings)

- 职责：组合 depthBias + normalBias。
- 说明：可减少接触阴影 acne 与 peter-panning。


## 5. 调用位置（必须修改的点）

## 5.1 Rasterizer 阴影分支替换

文件：[src/Rasterizer.cpp](src/Rasterizer.cpp)

在 RasterizeTriangleMSAA 内当前这段逻辑：

- Point 分支里直接比较 receiverDepth 与 blockerDepth
- Directional 分支里直接比较 receiverDepth 与 blockerDepth

替换为统一调用：

- frag.shadowVisibility = EvaluateShadowVisibility(scene, light, worldPos, frag.normal, bias)

这样 Hard/PCF/PCSS 都走同一入口，后续维护最简单。


## 5.2 DebugUI 参数接入（建议）

文件：[src/DebugUI.cpp](src/DebugUI.cpp)

在 Shadow Settings 面板补充：

- Filter Mode 组合框（Hard / PCF / PCSS）
- PCF 参数：kernelRadius、sampleCount
- PCSS 参数：blockerSearchSamples、filterSamples、lightSize

说明：这些字段在 [include/Scene.h](include/Scene.h) 已存在，主要是 UI 暴露与约束范围。


## 5.3 Shadow Pass 是否修改

文件：[src/software_renderer.cpp](src/software_renderer.cpp)

第一阶段可不修改：

- PCF/PCSS 只影响第二阶段采样，不影响深度图生成。

第二阶段优化（可选）：

- 若 Point 光 PCSS 质量不足，再考虑改为写入“线性深度”或“光源距离深度”。


## 6. 推荐实现顺序

1. 先抽出 EvaluateShadowVisibility + Hard 模式回归

- 目标：不改画质，只做结构重构，确保行为不变。

2. 实现 PCF（Directional）

- 先固定核（3x3 或 poisson16），确认稳定性。

3. 实现 PCF（Point）

- 先只在当前 face 内采样；面边界伪影后续再优化。

4. 实现 PCSS（Directional）

- 先跑通 blocker 搜索与可变半径逻辑。

5. 实现 PCSS（Point）

- 使用同样三阶段框架；必要时再做点光深度线性化增强。


## 7. 参数建议（初始值）

- PCF
  - pcfKernelRadius = 1~2
  - pcfSampleCount = 16

- PCSS
  - pcssBlockerSearchSamples = 16
  - pcssFilterSamples = 32
  - pcssLightSize = 0.02 ~ 0.05


## 8. 验收标准

- Hard / PCF / PCSS 三模式切换时均无崩溃。
- PCF：阴影边缘明显变软。
- PCSS：接触处更硬、远离接触处更软，半影宽度随距离变化。
- Point 光移动时，PCF/PCSS 阴影稳定，无明显抖动。
