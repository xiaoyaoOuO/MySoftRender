#pragma once
#include<vector>
#include<memory>
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Light.h"
using std::vector;
using std::unique_ptr;  
class Scene
{
public:
    vector<unique_ptr<Object>> objects; // 场景中的物体列表
    unique_ptr<Camera> camera; // 场景中的相机
    vector<unique_ptr<Light>> lights; // 场景中的光源

    glm::vec3 ambientLightColor = glm::vec3(1.0f); // 场景的环境光颜色
    float ambientLightIntensity = 0.5f; // 场景的环境光强度

public:
    void RotateObjects(float deltaTime)
    {
        // for (const auto& obj : objects) {
        //     obj->rotate(glm::vec3(0.0f, 30.0f * deltaTime, 0.0f)); // 每秒绕Y轴旋转30度
        // }
    }
};