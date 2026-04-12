#include "software_renderer.h"
#include "Sphere.h"
#include "Cube.h"
#include "MeshObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace {
// 作用：把阴影贴图二维坐标映射到一维深度缓存索引。
// 用法：传入像素坐标与贴图宽度，返回 lightViewDepths 的线性下标。
size_t ShadowDepthIndex(int x, int y, int width)
{
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

// 作用：将三角形从裁剪空间投影到指定分辨率的目标平面（可用于屏幕或阴影图）。
// 用法：传入 VP 矩阵与目标宽高，函数会原地写回投影后的顶点坐标。
bool ProjectTriangleToTarget(const glm::mat4& mvp, std::array<Vertex, 3>& vertices, int targetWidth, int targetHeight)
{
    glm::vec4 clipVertices[3];
    for (int i = 0; i < 3; ++i) {
        clipVertices[i] = mvp * glm::vec4(vertices[i].position, 1.0f);
    }

    constexpr float kClipEpsilon = 1e-6f;
    for (int i = 0; i < 3; ++i) {
        if (clipVertices[i].w <= kClipEpsilon) {
            return false;
        }
    }

    const auto allOutside = [&](auto planeOut) {
        return planeOut(clipVertices[0])
            && planeOut(clipVertices[1])
            && planeOut(clipVertices[2]);
    };

    if (allOutside([](const glm::vec4& v) { return v.x < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.x > v.w; })
        || allOutside([](const glm::vec4& v) { return v.y < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.y > v.w; })
        || allOutside([](const glm::vec4& v) { return v.z < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.z > v.w; })) {
        return false;
    }

    glm::vec4 ndcVertices[3];
    for (int i = 0; i < 3; ++i) {
        // 作用：缓存 1/w 供主 Pass 做透视校正插值，避免 worldPos/UV 随相机移动产生游动。
        // 用法：该字段会在光栅化阶段参与重心插值修正。
        vertices[i].invW = 1.0f / clipVertices[i].w;
        ndcVertices[i] = clipVertices[i] / clipVertices[i].w;
    }

    for (int i = 0; i < 3; ++i) {
        vertices[i].position.x = (ndcVertices[i].x + 1.0f) * 0.5f * static_cast<float>(targetWidth);
        vertices[i].position.y = (1.0f - (ndcVertices[i].y + 1.0f) * 0.5f) * static_cast<float>(targetHeight);
        vertices[i].position.z = (ndcVertices[i].z + 1.0f) * 0.5f;
    }
    return true;
}

// 作用：在第一 Pass 将单个三角形深度写入光源视角深度缓存。
// 用法：输入已投影到阴影贴图坐标系的三角形顶点，函数会执行深度测试并写入最近深度。
void RasterizeShadowDepthTriangle(const std::array<Vertex, 3>& vertices, int width, int height, std::vector<float>& shadowDepth)
{
    int minX = static_cast<int>(std::floor(std::min({vertices[0].position.x, vertices[1].position.x, vertices[2].position.x})));
    int maxX = static_cast<int>(std::ceil(std::max({vertices[0].position.x, vertices[1].position.x, vertices[2].position.x})));
    int minY = static_cast<int>(std::floor(std::min({vertices[0].position.y, vertices[1].position.y, vertices[2].position.y})));
    int maxY = static_cast<int>(std::ceil(std::max({vertices[0].position.y, vertices[1].position.y, vertices[2].position.y})));

    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, width - 1);
    maxY = std::min(maxY, height - 1);
    if (minX > maxX || minY > maxY) {
        return;
    }

    const Vec2 v0 = {vertices[0].position.x, vertices[0].position.y};
    const Vec2 v1 = {vertices[1].position.x, vertices[1].position.y};
    const Vec2 v2 = {vertices[2].position.x, vertices[2].position.y};
    const float area = VectorMath::edgeFunction(v0, v1, v2);
    if (std::abs(area) <= 1e-6f) {
        return;
    }

    const bool isAreaPositive = area > 0.0f;
    const float invArea = 1.0f / area;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const Vec2 p = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            float w0 = VectorMath::edgeFunction(v1, v2, p);
            float w1 = VectorMath::edgeFunction(v2, v0, p);
            float w2 = VectorMath::edgeFunction(v0, v1, p);

            const bool inside = isAreaPositive
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) {
                continue;
            }

            w0 *= invArea;
            w1 *= invArea;
            w2 *= invArea;

            const float depth = vertices[0].position.z * w0
                + vertices[1].position.z * w1
                + vertices[2].position.z * w2;

            const size_t index = ShadowDepthIndex(x, y, width);
            if (depth < shadowDepth[index]) {
                shadowDepth[index] = depth;
            }
        }
    }
}
}



bool SoftwareRenderer::projectLocalTriangleToScreen(const glm::mat4& mvp,std::array<Vertex, 3>& vertices) {
    // 作用：在齐次除法前做保守裁剪拒绝，避免相机后方几何被镜像到屏幕前方。
    // 用法：先在裁剪空间检查 w 与六个裁剪平面，任一拒绝条件命中则直接丢弃该三角形。
    glm::vec4 clipVertices[3];
    for (int i = 0; i < 3; ++i) {
        clipVertices[i] = mvp * glm::vec4(vertices[i].position, 1.0f);
    }

    constexpr float kClipEpsilon = 1e-6f;
    for (int i = 0; i < 3; ++i) {
        if (clipVertices[i].w <= kClipEpsilon) {
            return false;
        }
    }

    const auto allOutside = [&](auto planeOut) {
        return planeOut(clipVertices[0])
            && planeOut(clipVertices[1])
            && planeOut(clipVertices[2]);
    };

    if (allOutside([](const glm::vec4& v) { return v.x < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.x > v.w; })
        || allOutside([](const glm::vec4& v) { return v.y < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.y > v.w; })
        || allOutside([](const glm::vec4& v) { return v.z < -v.w; })
        || allOutside([](const glm::vec4& v) { return v.z > v.w; })) {
        return false;
    }

    glm::vec4 ndcVertices[3];
    for (int i = 0; i < 3; ++i) {
        // 作用：缓存当前顶点投影后的 1/w，供片段阶段做透视正确的属性插值。
        // 用法：后续 worldPos/normal/UV 插值应基于该值修正，避免阴影与纹理随视角漂移。
        vertices[i].invW = 1.0f / clipVertices[i].w;
        ndcVertices[i] = clipVertices[i] / clipVertices[i].w;
    }

    for (int i = 0; i < 3; ++i) {
        vertices[i].position.x = (ndcVertices[i].x + 1.0f) * 0.5f * width_;
        vertices[i].position.y = (1.0f - (ndcVertices[i].y + 1.0f) * 0.5f) * height_;
        vertices[i].position.z = (ndcVertices[i].z + 1.0f) * 0.5f;
    }

    return true;
};

void SoftwareRenderer::rasterizeLocalTriangle(const glm::mat4& model,const glm::mat4& mvp,const std::array<Vertex, 3>& localVertices,const Texture2D* texture) {
    std::array<glm::vec3, 3> worldPositions;
    std::array<glm::vec3, 3> worldNormals;

    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    const glm::vec3 fallbackLocalNormal = VectorMath::getNormal(
        localVertices[0].position,
        localVertices[1].position,
        localVertices[2].position);

    for (int i = 0; i < 3; ++i) {
        const glm::vec4 worldPos4 = model * glm::vec4(localVertices[i].position, 1.0f);
        worldPositions[i] = glm::vec3(worldPos4);

        glm::vec3 localNormal = localVertices[i].normal;
        if (glm::dot(localNormal, localNormal) <= 1e-12f) {
            localNormal = fallbackLocalNormal;
        }

        glm::vec3 transformedNormal = normalMatrix * localNormal;
        if (glm::dot(transformedNormal, transformedNormal) <= 1e-12f) {
            transformedNormal = fallbackLocalNormal;
        }
        worldNormals[i] = glm::normalize(transformedNormal);
    }

    std::array<Vertex, 3> screenVertices = localVertices;
    if (!projectLocalTriangleToScreen(mvp, screenVertices)) {
        return;
    }

    rasterizer_.Rasterize_Triangle(screenVertices, texture, &worldPositions, &worldNormals);
};


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

bool SoftwareRenderer::backfaceCullingEnabled() const
{
    return rasterizer_.backfaceCullingEnabled();
}

void SoftwareRenderer::setBackfaceCullingEnabled(bool enabled)
{
    rasterizer_.setBackfaceCullingEnabled(enabled);
}

void SoftwareRenderer::toggleBackfaceCulling()
{
    rasterizer_.toggleBackfaceCulling();
}

int SoftwareRenderer::msaaSampleCount() const
{
    return rasterizer_.msaaSampleCount();
}

void SoftwareRenderer::setMsaaSampleCount(int sampleCount)
{
    rasterizer_.setMsaaSampleCount(sampleCount);
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

    //第一个Pass Light & Shadow Map Pass：遍历场景中的光源，生成独立阴影深度图。
    const auto& lights = scene.lights;
    Light* mainLight = nullptr;
    if(!lights.empty()){
        mainLight = lights[0].get();
    }

    if(scene.shadowSettings.enableShadowMap && mainLight && mainLight->castShadowEnabled())
    {
        // 作用：让光源深度缓存尺寸与软件渲染颜色缓冲保持一致。
        // 用法：第一 Pass 写入的 depthbuffer 使用当前 renderer 的 width_/height_。
        const int shadowWidth = width_;
        const int shadowHeight = height_;
        mainLight->setShadowMapSize(shadowWidth, shadowHeight);
        std::vector<float>& lightDepths = mainLight->lightViewDepths();
        const size_t requiredSize = static_cast<size_t>(shadowWidth) * static_cast<size_t>(shadowHeight);
        if (lightDepths.size() != requiredSize) {
            lightDepths.assign(requiredSize, std::numeric_limits<float>::infinity());
        } else {
            std::fill(lightDepths.begin(), lightDepths.end(), std::numeric_limits<float>::infinity());
        }

        const glm::mat4 lightMatrix = mainLight->lightSpaceMatrix();

        for(const auto& obj : scene.objects)
        {
            const glm::mat4 model = obj->modelMatrix();
            const glm::mat4 mvp = lightMatrix * model;

            if (const MeshObject* meshObject = dynamic_cast<MeshObject*>(obj.get())) {
                if (!meshObject->hasMesh()) {
                    continue;
                }

                const ObjMeshData& mesh = meshObject->mesh();
                for (const glm::uvec3& face : mesh.indices) {
                    std::array<Vertex, 3> localVertices;
                    bool faceValid = true;
                    for (int i = 0; i < 3; ++i) {
                        const std::uint32_t vertexIndex = face[i];
                        if (vertexIndex >= mesh.vertices.size()) {
                            faceValid = false;
                            break;
                        }
                        localVertices[i] = mesh.vertices[vertexIndex];
                    }
                    if (!faceValid) {
                        continue;
                    }

                    std::array<Vertex, 3> lightVertices = localVertices;
                    if (!ProjectTriangleToTarget(mvp, lightVertices, shadowWidth, shadowHeight)) {
                        continue;
                    }
                    RasterizeShadowDepthTriangle(lightVertices, shadowWidth, shadowHeight, lightDepths);
                }
            } else if (const Triangle* tri = dynamic_cast<Triangle*>(obj.get())) {
                std::array<Vertex, 3> localVertices;
                for (int i = 0; i < 3; ++i) {
                    localVertices[i].position = glm::vec3(tri->getVertexs()[i]);
                }
                std::array<Vertex, 3> lightVertices = localVertices;
                if (!ProjectTriangleToTarget(mvp, lightVertices, shadowWidth, shadowHeight)) {
                    continue;
                }
                RasterizeShadowDepthTriangle(lightVertices, shadowWidth, shadowHeight, lightDepths);
            }else if (const Sphere* sphere = dynamic_cast<Sphere*>(obj.get())) {
                const auto& sphereVertices = sphere->vertices();
                const auto& sphereIndices = sphere->indices();

                for (const glm::uvec3& face : sphereIndices) {
                    std::array<Vertex, 3> localVertices;
                    for (int i = 0; i < 3; ++i) {
                        int idx = face[i];
                        localVertices[i].position = glm::vec3(sphereVertices[idx]);
                    }
                    std::array<Vertex, 3> lightVertices = localVertices;
                    if (!ProjectTriangleToTarget(mvp, lightVertices, shadowWidth, shadowHeight)) {
                        continue;
                    }
                    RasterizeShadowDepthTriangle(lightVertices, shadowWidth, shadowHeight, lightDepths);
                }
            }else if(const Cube* cube = dynamic_cast<Cube*>(obj.get())) {
                const auto& cubeVertices = cube->vertices();
                const auto& cubeIndices = cube->indices();

                for (const glm::uvec3& face : cubeIndices) {
                    std::array<Vertex, 3> localVertices;
                    for (int i = 0; i < 3; ++i) {
                        localVertices[i].position = cubeVertices[face[i]];
                    }
                    std::array<Vertex, 3> lightVertices = localVertices;
                    if (!ProjectTriangleToTarget(mvp, lightVertices, shadowWidth, shadowHeight)) {
                        continue;
                    }
                    RasterizeShadowDepthTriangle(lightVertices, shadowWidth, shadowHeight, lightDepths);
                }
            }
        }
    }
    
    
    //先做MVP变换，得到屏幕空间坐标，然后调用 Rasterize_Triangle() 函数进行光栅化。
    glm::mat4 viewProjection = scene.camera->projectionMatrix() * scene.camera->viewMatrix();

    for(const auto& obj : scene.objects)
    {
        const glm::mat4 model = obj->modelMatrix();
        const glm::mat4 mvp = viewProjection * model;
        const Texture2D* objectTexture = obj->hasTexture() ? obj->texture().get() : nullptr;

        if (const MeshObject* meshObject = dynamic_cast<MeshObject*>(obj.get())) {
            if (!meshObject->hasMesh()) {
                continue;
            }

            const ObjMeshData& mesh = meshObject->mesh();
            for (const glm::uvec3& face : mesh.indices) {
                std::array<Vertex, 3> localVertices;
                bool faceValid = true;
                for (int i = 0; i < 3; ++i) {
                    const std::uint32_t vertexIndex = face[i];
                    if (vertexIndex >= mesh.vertices.size()) {
                        faceValid = false;
                        break;
                    }
                    localVertices[i] = mesh.vertices[vertexIndex];
                }
                if (!faceValid) {
                    continue;
                }
                rasterizeLocalTriangle(model, mvp, localVertices, objectTexture);
            }
        } else if (const Triangle* tri = dynamic_cast<Triangle*>(obj.get())) {
            std::array<Vertex, 3> localVertices;
            for (int i = 0; i < 3; ++i) {
                localVertices[i].position = glm::vec3(tri->getVertexs()[i]);
                localVertices[i].color = tri->getColors()[i];
                localVertices[i].texCoord = tri->getTexCoords()[i];
                localVertices[i].normal = tri->getNormal();
            }
            rasterizeLocalTriangle(model, mvp, localVertices, objectTexture);
        }else if (const Sphere* sphere = dynamic_cast<Sphere*>(obj.get())) {
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

                    const glm::vec3 normal = glm::vec3(sphereVertices[idx]);
                    if (glm::dot(normal, normal) <= 1e-12f) {
                        localVertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else {
                        localVertices[i].normal = glm::normalize(normal);
                    }
                }
                rasterizeLocalTriangle(model, mvp, localVertices, objectTexture);
            }
        }else if(const Cube* cube = dynamic_cast<Cube*>(obj.get())) {
            const auto& cubeVertices = cube->vertices();
            const auto& cubeIndices = cube->indices();
            const auto& cubeColor = cube->color();

            for (const glm::uvec3& face : cubeIndices) {
                std::array<Vertex, 3> localVertices;
                for (int i = 0; i < 3; ++i) {
                    localVertices[i].position = cubeVertices[face[i]];
                    localVertices[i].color = cubeColor;

                    const glm::vec3 normal = cubeVertices[face[i]];
                    if (glm::dot(normal, normal) <= 1e-12f) {
                        localVertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else {
                        localVertices[i].normal = glm::normalize(normal);
                    }
                }
                rasterizeLocalTriangle(model, mvp, localVertices);
            }
        }
    }

    //Fragment Shader：遍历光栅化阶段生成的片段，进行深度测试和颜色写入。
    for(const auto& frag : rasterizer_.fragments())
    {
        if (fragmentShader_) {
            fragmentShader_(colorBuffer_, frag, scene);
        } else {
            putPixel(frag.bufferIndex, frag.color);
        }
    }
}