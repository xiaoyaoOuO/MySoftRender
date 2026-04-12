#pragma once
#include<vector>
#include<memory>
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Light.h"
using std::vector;
using std::unique_ptr;  

// 作用：定义阴影过滤模式。
// 用法：当前先实现 Hard，PCF/PCSS 先保留分支入口，便于后续逐步补全算法。
enum class ShadowFilterMode
{
    Hard = 0,
    PCF = 1,
    PCSS = 2
};

// 作用：集中管理阴影渲染参数，避免参数分散在主循环和渲染器内部。
// 用法：运行时可通过调试面板修改本结构字段，下一帧立即生效。
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

class Scene
{
public:
    vector<unique_ptr<Object>> objects; // 场景中的物体列表
    unique_ptr<Camera> camera; // 场景中的相机
    vector<unique_ptr<Light>> lights; // 场景中的光源

    glm::vec3 ambientLightColor = glm::vec3(1.0f); // 场景的环境光颜色
    float ambientLightIntensity = 0.5f; // 场景的环境光强度
    ShadowSettings shadowSettings; // 场景阴影配置

    inline static Scene* instance = nullptr; // 场景单例实例

public:
    void RotateObjects(float deltaTime)
    {
        // for (const auto& obj : objects) {
        //     obj->rotate(glm::vec3(0.0f, 30.0f * deltaTime, 0.0f)); // 每秒绕Y轴旋转30度
        // }
    }
};