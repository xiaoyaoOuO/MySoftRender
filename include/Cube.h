#pragma once

#include "Object.h"

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
    }

    const glm::vec3& center() const { return position; }
    const glm::vec3& size() const { return size_; }
    const glm::vec4& color() const { return color_; }

    void setCenter(const glm::vec3& center) { position = center; }
    void setSize(const glm::vec3& size) { size_ = size; }
    void setColor(const glm::vec4& color) { color_ = color; }

    glm::vec3 minCorner() const { return position - size_ * 0.5f; }
    glm::vec3 maxCorner() const { return position + size_ * 0.5f; }

private:
    glm::vec3 size_;
    glm::vec4 color_;
};
