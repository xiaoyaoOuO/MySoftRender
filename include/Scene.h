#pragma once
#include<vector>
#include<memory>
#include<string>
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Light.h"
using std::vector;
using std::unique_ptr;  

class CubemapTexture;

// 定义阴影过滤模式。当前先实现 Hard，PCF/PCSS 先保留分支入口，便于后续逐步补全算法。
enum class ShadowFilterMode
{
    Hard = 0,
    PCF = 1,
    PCSS = 2
};

// 集中管理阴影渲染参数，避免参数分散在主循环和渲染器内部。运行时可通过调试面板修改本结构字段，下一帧立即生效。
struct ShadowSettings
{
    bool enableShadowMap = true;
    int shadowMapResolution = 1024;
    float depthBias = 0.0015f;
    float normalBias = 0.02f;
    ShadowFilterMode filterMode = ShadowFilterMode::Hard;

    // 预留参数：本轮先不实现完整 PCF/PCSS，仅用于后续算法接入。
    int pcfKernelRadius = 1;
    int pcfSampleCount = 16;
    int pcssBlockerSearchSamples = 16;
    int pcssFilterSamples = 32;
    float pcssLightSize = 0.03f;
};

/**
 * @brief IBL 环境光照配置（阶段 A：仅启用 Diffuse IBL）。
 */
struct IBLSettings
{
    bool enableIBL = true;
    bool enableDiffuseIBL = true;
    float diffuseIntensity = 0.25f;
};

class Scene
{
public:
    vector<unique_ptr<Object>> objects; // 场景中的物体列表
    unique_ptr<Camera> camera; // 场景中的相机
    vector<unique_ptr<Light>> lights; // 场景中的光源

    glm::vec3 ambientLightColor = glm::vec3(1.0f); // 场景的环境光颜色
    float ambientLightIntensity = 0.5f; // 场景的环境光强度
    IBLSettings iblSettings; // 场景 IBL 配置（运行时可调）
    std::shared_ptr<CubemapTexture> iblIrradianceMap; // Diffuse IBL 使用的 irradiance 资源（为空时回退 skyboxTexture）
    ShadowSettings shadowSettings; // 场景阴影配置
    bool enableSkybox = true; // 是否启用天空盒背景绘制
    std::shared_ptr<CubemapTexture> skyboxTexture; // 当前场景绑定的天空盒资源
    std::string skyboxName; // 当前天空盒显示名称（用于日志与调试 UI）
    int lightProxyObjectIndex = -1; // 光源可视化代理对象在 objects 中的索引（-1 表示无代理）

    inline static Scene* instance = nullptr; // 场景单例实例

public:
    void RotateObjects(float deltaTime)
    {
        // for (const auto& obj : objects) {
        //     obj->rotate(glm::vec3(0.0f, 30.0f * deltaTime, 0.0f)); // 每秒绕Y轴旋转30度
        // }
    }
};
