#include "software_renderer.h"
#include "Sphere.h"
#include "Cube.h"

#include <algorithm>
#include <array>
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

bool SoftwareRenderer::wireframeOverlayEnabled() const
{
    return rasterizer_.wireframeOverlayEnabled();
}

void SoftwareRenderer::setWireframeOverlayEnabled(bool enabled)
{
    rasterizer_.setWireframeOverlayEnabled(enabled);
}

void SoftwareRenderer::toggleWireframeOverlay()
{
    rasterizer_.toggleWireframeOverlay();
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

void SoftwareRenderer::putPixel(size_t index, const Color& color)
{
    if (index < colorBuffer_.size()) {
        colorBuffer_[index] = packColor(color);
    }
}

void SoftwareRenderer::DrawScene(const Scene& scene)
{
    // 这里应该实现渲染算法，遍历场景中的物体并将它们渲染到 colorBuffer_ 中。
    if (!scene.camera) {
        return;
    }

    rasterizer_.Clear();
    
    //先做MVP变换，得到屏幕空间坐标，然后调用drawTriangle()函数进行光栅化。
    const glm::mat4 viewProjection = scene.camera->projectionMatrix() * scene.camera->viewMatrix();

    auto rasterizeLocalTriangle = [&](const glm::mat4& mvp,
                                     const std::array<glm::vec4, 3>& localVertices,
                                     const std::array<glm::vec3, 3>& colors) {
        glm::vec4 ndcVertices[3];
        for (int i = 0; i < 3; ++i) {
            const glm::vec4 clipVertex = mvp * localVertices[i];
            if (std::abs(clipVertex.w) <= 1e-6f) {
                return;
            }
            ndcVertices[i] = clipVertex / clipVertex.w;
        }

        std::array<glm::vec3, 3> screenVertices;
        for (int i = 0; i < 3; ++i) {
            screenVertices[i].x = (ndcVertices[i].x + 1.0f) * 0.5f * width_;
            screenVertices[i].y = (1.0f - (ndcVertices[i].y + 1.0f) * 0.5f) * height_;
            screenVertices[i].z = (ndcVertices[i].z + 1.0f) * 0.5f;
        }

        rasterizer_.Rasterize_Triangle(screenVertices, colors);
    };

    for(const auto& obj : scene.objects)
    {
        const glm::mat4 mvp = viewProjection * obj->modelMatrix();

        if (const Triangle* tri = dynamic_cast<Triangle*>(obj.get())) {
            const std::array<glm::vec4, 3> localVertices = {
                tri->getVertexs()[0],
                tri->getVertexs()[1],
                tri->getVertexs()[2]
            };
            const std::array<glm::vec3, 3> colors = {
                tri->getColors()[0],
                tri->getColors()[1],
                tri->getColors()[2]
            };
            rasterizeLocalTriangle(mvp, localVertices, colors);
            continue;
        }

        if (const Sphere* sphere = dynamic_cast<Sphere*>(obj.get())) {
            const auto& sphereVertices = sphere->vertices();
            const auto& sphereIndices = sphere->indices();
            const auto& sphereColors = sphere->vertexColors();

            for (const glm::uvec3& face : sphereIndices) {
                const std::array<glm::vec4, 3> localVertices = {
                    sphereVertices[face.x],
                    sphereVertices[face.y],
                    sphereVertices[face.z]
                };
                const std::array<glm::vec3, 3> colors = {
                    sphereColors[face.x],
                    sphereColors[face.y],
                    sphereColors[face.z]
                };
                rasterizeLocalTriangle(mvp, localVertices, colors);
            }
        }

        if(const Cube* cube = dynamic_cast<Cube*>(obj.get())) {
            const auto& cubeVertices = cube->vertices();
            const auto& cubeIndices = cube->indices();
            const auto& cubeColor = cube->color();

            for (const glm::uvec3& face : cubeIndices) {
                const std::array<glm::vec4, 3> localVertices = {
                    glm::vec4(cubeVertices[face.x], 1.0f),
                    glm::vec4(cubeVertices[face.y], 1.0f),
                    glm::vec4(cubeVertices[face.z], 1.0f)
                };
                const std::array<glm::vec3, 3> colors = {
                    cubeColor,
                    cubeColor,
                    cubeColor
                };
                rasterizeLocalTriangle(mvp, localVertices, colors);
            }
        }
    }

    //Fragment Shader：遍历光栅化阶段生成的片段，进行深度测试和颜色写入。
    for(const auto& frag : rasterizer_.fragments())
    {
        if (fragmentShader_) {
            fragmentShader_(colorBuffer_, frag);
        } else {
            putPixel(frag.bufferIndex, frag.color);
        }
    }
}