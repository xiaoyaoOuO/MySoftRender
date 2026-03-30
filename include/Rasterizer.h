#pragma once
#include "Triangle.h"
#include <vector>
#include <cmath>
#include <algorithm>

struct Vec2 {
    float x;
    float y;
};

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

struct Fragment
{
    Vec2 screenPos; // 屏幕空间坐标
    float depth; // 深度值
    Color color; // 颜色
    glm::vec3 normal; // 法线向量
};

namespace VectorMath{
    //根据重心坐标进行颜色插值
    Color InterpColor(const std::vector<glm::vec3>& colors, const std::vector<float>& weights)
    {
        glm::vec3 color(0.0f);
        for (size_t i = 0; i < colors.size() && i < weights.size(); ++i) {
            color += colors[i] * weights[i];
        }
        return Color{
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.r * 255.0f), 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.g * 255.0f), 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.b * 255.0f), 0, 255)),
            255
        };
    }

    //根据重心坐标进行深度插值
    float InterpDepth(const std::vector<Vec2>& vertexs, const std::vector<float>& weights)
    {
        float depth = 0.0f;
        for (size_t i = 0; i < vertexs.size() && i < weights.size(); ++i) {
            depth += vertexs[i].y * weights[i]; // 这里假设 vertexs[i].y 存储了深度值
        }
        return depth;
    }

    glm::vec3 getNormal(const glm::vec4& v0, const glm::vec4& v1, const glm::vec4& v2)
    {
        return glm::normalize(glm::cross(glm::vec3(v1 - v0), glm::vec3(v2 - v0)));
    }

    // 有符号面积辅助函数，用于重心坐标/点在三角形内判断。
    float edgeFunction(const Vec2& a, const Vec2& b, const Vec2& p)
    {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    }
}

namespace {
    size_t BufferIndex(int x, int y, int width) {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    }
}

class Rasterizer {
public:
    Rasterizer(int width, int height): width_(width), height_(height) 
    {
        zBuffer_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), std::numeric_limits<float>::infinity());
    }

    int width() const;
    int height() const;

    void Rasterize_Triangle(std::vector<glm::vec3>& vertexs, std::vector<glm::vec3>& colors);

private:
    int width_;
    int height_;

    std::vector<float> zBuffer_; // 深度缓冲
    std::unordered_map<std::size_t, Fragment> fragments_; // 存储片段数据，key 可以是像素坐标的哈希值
};