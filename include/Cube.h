#pragma once

#include "Object.h"

#include <array>
#include <glm/glm.hpp>

class Cube : public Object
{
public:
    Cube(
        const glm::vec3& center = glm::vec3(0.0f, 0.0f, 0.0f),
        const glm::vec3& size = glm::vec3(1.0f, 1.0f, 1.0f),
        const glm::vec4& color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        const glm::vec3& rotation = glm::vec3(0.0f, 0.0f, 0.0f),
        const glm::vec3& scale = glm::vec3(1.0f, 1.0f, 1.0f))
        : Object(center, rotation, scale), size_(size), color_(color)
    {
        rebuildVertices();
    }

    const glm::vec3& center() const { return position; }
    const glm::vec3& size() const { return size_; }
    const glm::vec4& color() const { return color_; }

    void setCenter(const glm::vec3& center) { position = center; }
    void setSize(const glm::vec3& size)
    {
        size_ = size;
        rebuildVertices();
    }
    void setColor(const glm::vec4& color) { color_ = color; }

    glm::vec3 minCorner() const { return position - size_ * 0.5f; }
    glm::vec3 maxCorner() const { return position + size_ * 0.5f; }

    const std::array<glm::vec3, 8>& vertices() const { return vertices_; }
    const std::array<glm::uvec3, 12>& indices() const { return indices_; }

private:
    void rebuildVertices()
    {
        const glm::vec3 half = size_ * 0.5f;
        vertices_ = {
            glm::vec3(-half.x, -half.y, -half.z),
            glm::vec3( half.x, -half.y, -half.z),
            glm::vec3( half.x,  half.y, -half.z),
            glm::vec3(-half.x,  half.y, -half.z),
            glm::vec3(-half.x, -half.y,  half.z),
            glm::vec3( half.x, -half.y,  half.z),
            glm::vec3( half.x,  half.y,  half.z),
            glm::vec3(-half.x,  half.y,  half.z)
        };
    }

    glm::vec3 size_;
    glm::vec4 color_;
    std::array<glm::vec3, 8> vertices_; // 立方体的8个顶点
    //12个三角形面，每个面由3个顶点组成，索引如下：
    std::array<glm::uvec3, 12> indices_ = {
        glm::uvec3(0, 1, 2), glm::uvec3(0, 2, 3),
        glm::uvec3(1, 5, 6), glm::uvec3(1, 6, 2),
        glm::uvec3(5, 4, 7), glm::uvec3(5, 7, 6),
        glm::uvec3(4, 0, 3), glm::uvec3(4, 3, 7),
        glm::uvec3(3, 2, 6), glm::uvec3(3, 6, 7),
        glm::uvec3(4, 5, 1), glm::uvec3(4, 1, 0)
    };
};
