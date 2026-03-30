#include "software_renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>


SoftwareRenderer::SoftwareRenderer(int width, int height)
    // 为每个屏幕坐标分配一个 32 位像素。
    : width_(width), height_(height), rasterizer_(width, height)
{
    colorBuffer_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
}

void SoftwareRenderer::clear(const Color& color)
{
    // 使用打包后的 ARGB 颜色填充整个帧缓冲。
    std::fill(colorBuffer_.begin(), colorBuffer_.end(), packColor(color));
}


const std::uint32_t* SoftwareRenderer::colorBuffer() const
{
    return colorBuffer_.data();
}

int SoftwareRenderer::width() const
{
    return width_;
}

int SoftwareRenderer::height() const
{
    return height_;
}

std::uint32_t SoftwareRenderer::packColor(const Color& color)
{
    // 将颜色通道打包为 0xAARRGGBB，匹配 SDL ARGB8888 纹理格式。
    return (static_cast<std::uint32_t>(color.a) << 24U)
        | (static_cast<std::uint32_t>(color.r) << 16U)
        | (static_cast<std::uint32_t>(color.g) << 8U)
        | static_cast<std::uint32_t>(color.b);
}

void SoftwareRenderer::putPixel(int x, int y, const Color& color)
{
    // 将二维坐标转换为线性索引，并写入一个打包像素。
    colorBuffer_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = packColor(color);
}

void SoftwareRenderer::DrawScene(const Scene& scene)
{
    // 这里应该实现渲染算法，遍历场景中的物体并将它们渲染到 colorBuffer_ 中。
    if (!scene.camera) {
        return;
    }
    
    //先做MVP变换，得到屏幕空间坐标，然后调用drawTriangle()函数进行光栅化。
    const glm::mat4 viewProjection = scene.camera->projectionMatrix() * scene.camera->viewMatrix();
    for(const auto& obj : scene.objects)
    {
        glm::vec4 vertexs[3];
        Triangle* tri = dynamic_cast<Triangle*>(obj.get());
        if(tri == nullptr) continue; // 目前我们只处理三角形对象，其他类型的对象暂时跳过。

        const glm::mat4 mvp = viewProjection * tri->modelMatrix();
        for(int i = 0; i < 3; ++i)
        {
            vertexs[i] = mvp * tri->getVertexs()[i];
            if (std::abs(vertexs[i].w) <= 1e-6f) {
                vertexs[i].w = 1.0f;
            }
            // 齐次除法，得到屏幕空间坐标
            vertexs[i] /= vertexs[i].w;
        }
        // 将屏幕空间坐标转换为像素坐标
        std::vector<glm::vec3> colors(3);
        std::vector<glm::vec3> vertexs3D(3);
        for(int i = 0; i < 3; ++i)
        {
            vertexs3D[i] = glm::vec3(vertexs[i]);
            colors[i] = tri->getColors()[i];
            vertexs[i].x = (vertexs[i].x + 1.0f) * 0.5f * width_;
            vertexs[i].y = (1.0f - (vertexs[i].y + 1.0f) * 0.5f) * height_; // 注意 Y 轴翻转
            vertexs[i].z = (vertexs[i].z + 1.0f) * 0.5f; // 深度值映射到 [0, 1]
        }
        rasterizer_.Rasterize_Triangle(vertexs3D, colors);
    }

}

void SoftwareRenderer::drawTriangle(const std::vector<Vec2>& v, const std::vector<glm::vec3>& c)
{
    if (v.size() < 3 || c.size() < 3) {
        return;
    }

    int minX = static_cast<int>(std::floor(std::min({v[0].x, v[1].x, v[2].x})));
    int maxX = static_cast<int>(std::ceil(std::max({v[0].x, v[1].x, v[2].x})));
    int minY = static_cast<int>(std::floor(std::min({v[0].y, v[1].y, v[2].y})));
    int maxY = static_cast<int>(std::ceil(std::max({v[0].y, v[1].y, v[2].y})));

    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, width_ - 1);
    maxY = std::min(maxY, height_ - 1);

    if (minX > maxX || minY > maxY) {
        return;
    }

    float area = VectorMath::edgeFunction(v[0], v[1], v[2]);
    if (std::abs(area) <= 1e-6f) {
        return;
    }

    const bool isAreaPositive = area > 0.0f;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}; // 采样点在像素中心
            float w0 = VectorMath::edgeFunction(v[1], v[2], p);
            float w1 = VectorMath::edgeFunction(v[2], v[0], p);
            float w2 = VectorMath::edgeFunction(v[0], v[1], p);
            const bool inside = isAreaPositive
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (inside) { // 点在三角形内
                w0 /= area;
                w1 /= area;
                w2 /= area;
                float depth = VectorMath::InterpDepth(v, std::vector<float>{w0, w1, w2});
            }
        }
    }
}
