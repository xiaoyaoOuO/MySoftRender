#pragma once
#include "Object.h"

/**
 * @brief 代表一个三角形的类，继承自Object类。
 */
class Triangle : public Object
{
private:
    glm::vec4 vertexs[3]; // 每个顶点的齐次坐标
    glm::vec3 colors[3]; // 每个顶点的颜色
    glm::vec3 normal; // 三角形的法线向量
public:
    Triangle(
        const glm::vec4& v0 = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f),
        const glm::vec4& v1 = glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
        const glm::vec4& v2 = glm::vec4(0.5f, -0.5f, 0.0f, 1.0f),
        const glm::vec3& c0 = glm::vec3(1.0f, 0.0f, 0.0f),
        const glm::vec3& c1 = glm::vec3(0.0f, 1.0f, 0.0f),
        const glm::vec3& c2 = glm::vec3(0.0f, 0.0f, 1.0f)) : Object()
    {
        vertexs[0] = v0;
        vertexs[1] = v1;
        vertexs[2] = v2;
        colors[0] = c0;
        colors[1] = c1;
        colors[2] = c2;

        // 计算法线向量
        normal = glm::normalize(glm::cross(glm::vec3(v1 - v0), glm::vec3(v2 - v0)));
    }

    const glm::vec4* getVertexs() const { return vertexs; }
    const glm::vec3* getColors() const { return colors; }
    const glm::vec3& getNormal() const { return normal; }
};