#pragma once
#include<vector>
#include<memory>
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
using namespace std;
class Scene
{
public:
    vector<unique_ptr<Object>> objects; // 场景中的物体列表
    unique_ptr<Camera> camera; // 场景中的相机

public:
    void RotateObjects(float deltaTime)
    {
        for (const auto& obj : objects) {
            obj->rotate(glm::vec3(0.0f, 30.0f * deltaTime, 0.0f)); // 每秒绕Y轴旋转30度
        }
    }
};