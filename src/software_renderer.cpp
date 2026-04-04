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
    useDefaultSphereTexture();
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

bool SoftwareRenderer::loadSphereTexture(const std::string& texturePath)
{
    return sphereTexture_.loadFromFile(texturePath);
}

void SoftwareRenderer::useDefaultSphereTexture()
{
    sphereTexture_.createCheckerboard(
        1024,
        512,
        32,
        glm::vec3(0.95f, 0.95f, 0.95f),
        glm::vec3(0.12f, 0.38f, 0.78f));
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
    
    //先做MVP变换，得到屏幕空间坐标，然后调用 Rasterize_Triangle() 函数进行光栅化。
    const glm::mat4 viewProjection = scene.camera->projectionMatrix() * scene.camera->viewMatrix();

    auto projectLocalTriangleToScreen = [&](const glm::mat4& mvp,
                                            std::array<Vertex, 3>& vertices) {
        glm::vec4 ndcVertices[3];
        for (int i = 0; i < 3; ++i) {
            const glm::vec4 clipVertex = mvp * glm::vec4(vertices[i].position, 1.0f);
            if (std::abs(clipVertex.w) <= 1e-6f) {
                return false;
            }
            ndcVertices[i] = clipVertex / clipVertex.w;
        }

        for (int i = 0; i < 3; ++i) {
            vertices[i].position.x = (ndcVertices[i].x + 1.0f) * 0.5f * width_;
            vertices[i].position.y = (1.0f - (ndcVertices[i].y + 1.0f) * 0.5f) * height_;
            vertices[i].position.z = (ndcVertices[i].z + 1.0f) * 0.5f;
        }

        return true;
    };

    auto rasterizeLocalTriangle = [&](const glm::mat4& mvp,
                                      const std::array<Vertex, 3>& localVertices,
                                      const Texture2D* texture = nullptr) {
        std::array<Vertex, 3> screenVertices = localVertices;
        if (!projectLocalTriangleToScreen(mvp, screenVertices)) {
            return;
        }

        rasterizer_.Rasterize_Triangle(screenVertices, texture);
    };

    for(const auto& obj : scene.objects)
    {
        const glm::mat4 mvp = viewProjection * obj->modelMatrix();

        if (const Triangle* tri = dynamic_cast<Triangle*>(obj.get())) {
            std::array<Vertex, 3> localVertices;
            for (int i = 0; i < 3; ++i) {
                localVertices[i].position = glm::vec3(tri->getVertexs()[i]);
                localVertices[i].color = tri->getColors()[i];
            }
            rasterizeLocalTriangle(mvp, localVertices);
            continue;
        }

        if (const Sphere* sphere = dynamic_cast<Sphere*>(obj.get())) {
            const auto& sphereVertices = sphere->vertices();
            const auto& sphereIndices = sphere->indices();
            const auto& sphereColors = sphere->vertexColors();
            const auto& sphereUVs = sphere->vertexUVs();

            for (const glm::uvec3& face : sphereIndices) {
                std::array<Vertex, 3> localVertices;
                for (int i = 0; i < 3; ++i) {
                    int idx = face[i];
                    localVertices[i].position = glm::vec3(sphereVertices[idx]);
                    localVertices[i].color = sphereColors[idx];
                    localVertices[i].texCoord = sphereUVs[idx];
                }
                rasterizeLocalTriangle(mvp, localVertices, &sphereTexture_);
            }
            continue;
        }

        if(const Cube* cube = dynamic_cast<Cube*>(obj.get())) {
            const auto& cubeVertices = cube->vertices();
            const auto& cubeIndices = cube->indices();
            const auto& cubeColor = cube->color();

            for (const glm::uvec3& face : cubeIndices) {
                std::array<Vertex, 3> localVertices;
                for (int i = 0; i < 3; ++i) {
                    localVertices[i].position = cubeVertices[face[i]];
                    localVertices[i].color = cubeColor;
                }
                rasterizeLocalTriangle(mvp, localVertices);
            }
            continue;
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