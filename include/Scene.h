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
};