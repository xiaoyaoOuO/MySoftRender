#pragma once
#include "Triangle.h"
#include "ObjLoader.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

class Texture2D;

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
    size_t bufferIndex; // 在帧缓冲中的线性索引
    float depth; // 深度值
    Color color; // 颜色
    glm::vec3 normal; // 法线向量
};

struct Edge {
    Vec2 start;
    Vec2 end;
    Color colorStart;
    Color colorEnd;

    Edge(const glm::vec3& v0,const glm::vec3& v1){
        start = Vec2{std::min(v0.x, v1.x), std::min(v0.y, v1.y)};
        end = Vec2{std::max(v0.x, v1.x), std::max(v0.y, v1.y)};
    }
};

namespace VectorMath{
    //根据重心坐标进行颜色插值
    inline Color InterpColor(const std::vector<glm::vec3>& colors, const std::vector<float>& weights)
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
    inline float InterpDepth(const std::vector<glm::vec3>& vertexs, const std::vector<float>& weights)
    {
        float depth = 0.0f;
        for (size_t i = 0; i < vertexs.size() && i < weights.size(); ++i) {
            depth += vertexs[i].z * weights[i];
        }
        return depth;
    }

    inline glm::vec3 getNormal(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
    {
        return glm::normalize(glm::cross(v1 - v0, v2 - v0));
    }

    // 有符号面积辅助函数，用于重心坐标/点在三角形内判断。
    inline float edgeFunction(const Vec2& a, const Vec2& b, const Vec2& p)
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
        const std::size_t pixelCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        zBuffer_.resize(pixelCount, std::numeric_limits<float>::infinity());
        sampleZBuffer_.resize(pixelCount * kMsaaSampleCount, std::numeric_limits<float>::infinity());
        sampleColorBuffer_.resize(pixelCount * kMsaaSampleCount, glm::vec3(0.0f));
        sampleNormalBuffer_.resize(pixelCount * kMsaaSampleCount, glm::vec3(0.0f, 0.0f, 1.0f));
        fragmentLut_.resize(pixelCount, -1);
    }

    int width() const;
    int height() const;
    const std::vector<Fragment>& fragments() const { return fragments_; }
    bool wireframeOverlayEnabled() const { return wireframeOverlayEnabled_; }
    void setWireframeOverlayEnabled(bool enabled) { wireframeOverlayEnabled_ = enabled; }
    void toggleWireframeOverlay() { wireframeOverlayEnabled_ = !wireframeOverlayEnabled_; }

    // 作用：清除帧缓冲和所有相关的状态（如 ZBuffer，片段缓存等）
    void Clear();

    // 作用：将包含位置、颜色、法线、UV 坐标的 Vertex 结构体进行光栅化
    // 用法：可选择是否传入 texture 纹理指针，如果为空则走纯色填充逻辑
    void Rasterize_Triangle(const std::array<Vertex, 3>& vertices, const Texture2D* texture = nullptr);

private:
    static constexpr int kMsaaSampleCount = 4;

    int width_;
    int height_;
    bool wireframeOverlayEnabled_ = false;

    std::vector<float> zBuffer_; // 深度缓冲
    std::vector<float> sampleZBuffer_; // 每像素每采样点深度缓冲
    std::vector<glm::vec3> sampleColorBuffer_; // 每像素每采样点颜色
    std::vector<glm::vec3> sampleNormalBuffer_; // 每像素每采样点法线
    std::vector<int> fragmentLut_; // 像素索引到 fragments_ 下标映射
    std::vector<Fragment> fragments_; // 光栅化阶段生成的片段列表
};
