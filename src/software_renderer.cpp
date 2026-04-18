#include "software_renderer.h"
#include "Sphere.h"
#include "Cube.h"
#include "MeshObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

#include <glm/common.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace {
/**
 * @brief 把阴影贴图二维坐标映射到一维深度缓存索引。
 * @param x 阴影贴图像素 x 坐标。
 * @param y 阴影贴图像素 y 坐标。
 * @param width 阴影贴图宽度（像素）。
 * @return 返回深度缓存的一维线性下标。
 */
size_t ShadowDepthIndex(int x, int y, int width)
{
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

// 对方向向量做安全归一化，避免零向量在天空盒采样中产生 NaN。
glm::vec3 NormalizeVectorSafe(const glm::vec3& direction, const glm::vec3& fallback)
{
    if (glm::dot(direction, direction) <= 1e-12f) {
        return fallback;
    }
    return glm::normalize(direction);
}

enum class ClipPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far
};

/**
 * @brief 判断一个裁剪空间顶点是否落在指定平面外侧。
 * @param vertex 裁剪空间顶点（齐次坐标）。
 * @param plane 要检测的裁剪平面类型。
 * @return 返回 true 表示该顶点在指定平面外侧。
 */
bool IsVertexOutsideClipPlane(const glm::vec4& vertex, ClipPlane plane)
{
    switch (plane) {
    case ClipPlane::Left:
        return vertex.x < -vertex.w;
    case ClipPlane::Right:
        return vertex.x > vertex.w;
    case ClipPlane::Bottom:
        return vertex.y < -vertex.w;
    case ClipPlane::Top:
        return vertex.y > vertex.w;
    case ClipPlane::Near:
        return vertex.z < -vertex.w;
    case ClipPlane::Far:
        return vertex.z > vertex.w;
    default:
        return false;
    }
}

/**
 * @brief 判断三角形是否全部落在某一个裁剪平面外侧。
 * @param clipVertices 三角形三个裁剪空间顶点数组。
 * @param plane 要检测的裁剪平面类型。
 * @return 返回 true 表示整三角都在该平面外侧。
 */
bool IsTriangleOutsideClipPlane(const glm::vec4 clipVertices[3], ClipPlane plane)
{
    return IsVertexOutsideClipPlane(clipVertices[0], plane)
        && IsVertexOutsideClipPlane(clipVertices[1], plane)
        && IsVertexOutsideClipPlane(clipVertices[2], plane);
}

/**
 * @brief 执行六个裁剪平面的保守拒绝测试。
 * @param clipVertices 三角形三个裁剪空间顶点数组。
 * @return 返回 true 表示该三角形应被保守拒绝。
 */
bool IsTriangleRejectedByClipPlanes(const glm::vec4 clipVertices[3])
{
    return IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Left)
        || IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Right)
        || IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Bottom)
        || IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Top)
        || IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Near)
        || IsTriangleOutsideClipPlane(clipVertices, ClipPlane::Far);
}

/**
 * @brief 将三角形从裁剪空间投影到指定分辨率的目标平面。
 * @param mvp 当前投影使用的矩阵（可为相机 VP 或光源 VP）。
 * @param vertices 三角形顶点数组，函数会原地改写为目标平面坐标。
 * @param targetWidth 目标平面宽度（像素）。
 * @param targetHeight 目标平面高度（像素）。
 * @return 返回 true 表示投影成功且三角形可继续光栅化。
 */
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

    if (IsTriangleRejectedByClipPlanes(clipVertices)) {
        return false;
    }

    glm::vec4 ndcVertices[3];
    for (int i = 0; i < 3; ++i) {
        // 缓存 1/w 供主 Pass 做透视校正插值，避免 worldPos/UV 随相机移动产生游动。该字段会在光栅化阶段参与重心插值修正。
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

/**
 * @brief 在阴影 Pass 中将单个三角形深度写入光源视角深度缓存。
 * @param vertices 已投影到阴影图像素空间的三角形顶点。
 * @param width 阴影图宽度（像素）。
 * @param height 阴影图高度（像素）。
 * @param shadowDepth 目标阴影深度缓存。
 * @return 无返回值。
 */
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

/**
 * @brief 把场景对象的所有三角形投影到指定阴影目标并写入深度。
 * @param object 待投影对象（支持 MeshObject/Triangle/Sphere/Cube）。
 * @param mvp 当前阴影面的 MVP 矩阵（lightVP * model）。
 * @param targetWidth 目标深度图宽度（像素）。
 * @param targetHeight 目标深度图高度（像素）。
 * @param shadowDepthBuffer 目标深度缓存，函数内部会执行深度测试并写入。
 */
void RasterizeObjectShadowTriangles(
    const Object* object,
    const glm::mat4& mvp,
    int targetWidth,
    int targetHeight,
    std::vector<float>& shadowDepthBuffer)
{
    if (const MeshObject* meshObject = dynamic_cast<const MeshObject*>(object)) {
        if (!meshObject->hasMesh()) {
            return;
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
            if (!ProjectTriangleToTarget(mvp, lightVertices, targetWidth, targetHeight)) {
                continue;
            }
            RasterizeShadowDepthTriangle(lightVertices, targetWidth, targetHeight, shadowDepthBuffer);
        }
        return;
    }

    if (const Triangle* tri = dynamic_cast<const Triangle*>(object)) {
        std::array<Vertex, 3> localVertices;
        for (int i = 0; i < 3; ++i) {
            localVertices[i].position = glm::vec3(tri->getVertexs()[i]);
        }
        std::array<Vertex, 3> lightVertices = localVertices;
        if (!ProjectTriangleToTarget(mvp, lightVertices, targetWidth, targetHeight)) {
            return;
        }
        RasterizeShadowDepthTriangle(lightVertices, targetWidth, targetHeight, shadowDepthBuffer);
        return;
    }

    if (const Sphere* sphere = dynamic_cast<const Sphere*>(object)) {
        const auto& sphereVertices = sphere->vertices();
        const auto& sphereIndices = sphere->indices();

        for (const glm::uvec3& face : sphereIndices) {
            std::array<Vertex, 3> localVertices;
            for (int i = 0; i < 3; ++i) {
                const std::uint32_t idx = face[i];
                localVertices[i].position = glm::vec3(sphereVertices[idx]);
            }
            std::array<Vertex, 3> lightVertices = localVertices;
            if (!ProjectTriangleToTarget(mvp, lightVertices, targetWidth, targetHeight)) {
                continue;
            }
            RasterizeShadowDepthTriangle(lightVertices, targetWidth, targetHeight, shadowDepthBuffer);
        }
        return;
    }

    if (const Cube* cube = dynamic_cast<const Cube*>(object)) {
        const auto& cubeVertices = cube->vertices();
        const auto& cubeIndices = cube->indices();

        for (const glm::uvec3& face : cubeIndices) {
            std::array<Vertex, 3> localVertices;
            for (int i = 0; i < 3; ++i) {
                localVertices[i].position = cubeVertices[face[i]];
            }
            std::array<Vertex, 3> lightVertices = localVertices;
            if (!ProjectTriangleToTarget(mvp, lightVertices, targetWidth, targetHeight)) {
                continue;
            }
            RasterizeShadowDepthTriangle(lightVertices, targetWidth, targetHeight, shadowDepthBuffer);
        }
    }
}

/**
 * @brief 片元着色任务的只读/可写上下文。
 */
struct FragmentShadingContext
{
    const std::vector<Fragment>* fragments = nullptr;
    std::vector<std::uint32_t>* colorBuffer = nullptr;
    const std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)>* fragmentShader = nullptr;
    const Scene* scene = nullptr;
};

/**
 * @brief 执行指定区间的片元着色任务。
 * @param context 片元着色上下文。
 * @param beginIndex 区间起始片元索引（包含）。
 * @param endIndex 区间结束片元索引（不包含）。
 */
void ShadeFragmentRange(
    const FragmentShadingContext& context,
    std::size_t beginIndex,
    std::size_t endIndex)
{
    if (!context.fragments || !context.colorBuffer || !context.scene) {
        return;
    }

    const bool hasFragmentShader = (context.fragmentShader != nullptr) && static_cast<bool>(*context.fragmentShader);
    for (std::size_t fragIndex = beginIndex; fragIndex < endIndex; ++fragIndex) {
        const Fragment& frag = (*(context.fragments))[fragIndex];
        if (hasFragmentShader) {
            (*(context.fragmentShader))(*(context.colorBuffer), frag, *(context.scene));
        } else {
            (*(context.colorBuffer))[frag.bufferIndex] = SoftwareRenderer::packColor(frag.color);
        }
    }
}

/**
 * @brief 天空盒绘制的只读/可写上下文。
 */
struct SkyboxDrawContext
{
    std::vector<std::uint32_t>* colorBuffer = nullptr;
    const CubemapTexture* skyboxTexture = nullptr;
    const std::vector<glm::vec3>* localViewRays = nullptr;
    const std::vector<float>* depthBuffer = nullptr;
    glm::vec3 cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 cameraOrthoUp = glm::vec3(0.0f, 1.0f, 0.0f);
    bool hasDepthMask = false;
};

/**
 * @brief 执行指定区间的天空盒像素采样与写回。
 * @param context 天空盒绘制上下文。
 * @param beginIndex 区间起始像素索引（包含）。
 * @param endIndex 区间结束像素索引（不包含）。
 */
void DrawSkyboxPixelRange(
    const SkyboxDrawContext& context,
    std::size_t beginIndex,
    std::size_t endIndex)
{
    if (!context.colorBuffer || !context.skyboxTexture || !context.localViewRays) {
        return;
    }

    for (std::size_t pixelIndex = beginIndex; pixelIndex < endIndex; ++pixelIndex) {
        if (context.hasDepthMask && context.depthBuffer) {
            const float depth = (*(context.depthBuffer))[pixelIndex];
            // 只填充背景像素，避免对已被几何覆盖的前景重复执行天空盒采样。
            if (depth >= 0.0f && depth <= 1.0f) {
                continue;
            }
        }

        const glm::vec3& localRay = (*(context.localViewRays))[pixelIndex];
        const glm::vec3 rayDirection = localRay.x * context.cameraRight
            + localRay.y * context.cameraOrthoUp
            + localRay.z * context.cameraForward;
        const glm::vec3 skyColor = context.skyboxTexture->sample(rayDirection);

        Color color;
        color.r = static_cast<std::uint8_t>(std::clamp(skyColor.r, 0.0f, 1.0f) * 255.0f);
        color.g = static_cast<std::uint8_t>(std::clamp(skyColor.g, 0.0f, 1.0f) * 255.0f);
        color.b = static_cast<std::uint8_t>(std::clamp(skyColor.b, 0.0f, 1.0f) * 255.0f);
        color.a = 255;
        (*(context.colorBuffer))[pixelIndex] = SoftwareRenderer::packColor(color);
    }
}

/**
 * @brief 点光源阴影光栅化所需的静态对象数据。
 */
struct PointShadowCaster
{
    const Object* object = nullptr;
    glm::mat4 modelMatrix = glm::mat4(1.0f);
};

/**
 * @brief 执行指定区间点光阴影面的深度光栅化。
 * @param light 当前点光源对象。
 * @param shadowCasters 投射阴影的对象列表及其模型矩阵快照。
 * @param shadowResolution 点光阴影分辨率。
 * @param beginFace 区间起始面索引（包含）。
 * @param endFace 区间结束面索引（不包含）。
 */
void RasterizePointShadowFaceRange(
    Light& light,
    const std::vector<PointShadowCaster>& shadowCasters,
    int shadowResolution,
    std::size_t beginFace,
    std::size_t endFace)
{
    for (std::size_t face = beginFace; face < endFace; ++face) {
        std::vector<float>& faceDepths = light.pointLightViewDepths(face);
        const glm::mat4& faceMatrix = light.pointLightSpaceMatrix(face);

        for (const PointShadowCaster& caster : shadowCasters) {
            const glm::mat4 mvp = faceMatrix * caster.modelMatrix;
            RasterizeObjectShadowTriangles(caster.object, mvp, shadowResolution, shadowResolution, faceDepths);
        }
    }
}
}



bool SoftwareRenderer::projectLocalTriangleToScreen(const glm::mat4& mvp,std::array<Vertex, 3>& vertices) {
    // 在齐次除法前做保守裁剪拒绝，避免相机后方几何被镜像到屏幕前方。先在裁剪空间检查 w 与六个裁剪平面，任一拒绝条件命中则直接丢弃该三角形。
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

    if (IsTriangleRejectedByClipPlanes(clipVertices)) {
        return false;
    }

    glm::vec4 ndcVertices[3];
    for (int i = 0; i < 3; ++i) {
        // 缓存当前顶点投影后的 1/w，供片段阶段做透视正确的属性插值。后续 worldPos/normal/UV 插值应基于该值修正，避免阴影与纹理随视角漂移。
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


/**
 * @brief 将一个三角形从局部空间投影到屏幕空间并执行光栅化。
 * @param model 模型矩阵，用于将局部顶点变换到世界空间。
 * @param mvp 模型-视图-投影矩阵，用于将局部顶点投影到屏幕空间。
 * @param localVertices 三角形的三个局部空间顶点，包含位置、法线、纹理坐标等属性。
 * @param texture 可选的纹理指针，如果提供则在光栅化阶段进行纹理采样。
 * @param material 可选的材质弱指针，如果提供则在片段着色阶段使用材质属性进行光照计算。
 */
void SoftwareRenderer::rasterizeLocalTriangle(const glm::mat4& model,const glm::mat4& mvp,const std::array<Vertex, 3>& localVertices,const Texture2D* texture,std::weak_ptr<Material> material) {
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

    rasterizer_.Rasterize_Triangle(screenVertices, texture, &worldPositions, &worldNormals, material);
};


SoftwareRenderer::SoftwareRenderer(int width, int height)
    // 为每个屏幕坐标分配一个 32 位像素。
    : width_(width), height_(height), rasterizer_(width, height), fragmentShadingThreadPool_()
{
    colorBuffer_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    fragmentThreadingStats_.poolThreadCount = fragmentShadingThreadPool_.threadCount();
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

bool SoftwareRenderer::fragmentMultithreadingEnabled() const
{
    return fragmentMultithreadingEnabled_;
}

void SoftwareRenderer::setFragmentMultithreadingEnabled(bool enabled)
{
    fragmentMultithreadingEnabled_ = enabled;
}

const FragmentThreadingStats& SoftwareRenderer::fragmentThreadingStats() const
{
    return fragmentThreadingStats_;
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

void SoftwareRenderer::updateSkyboxViewRayCache(const Camera& camera)
{
    const bool cacheCompatible = skyboxViewRayCacheWidth_ == width_
        && skyboxViewRayCacheHeight_ == height_
        && std::abs(skyboxViewRayCacheFovYDeg_ - camera.fovYDeg()) <= 1e-6f
        && std::abs(skyboxViewRayCacheAspect_ - camera.aspectRatio()) <= 1e-6f
        && skyboxViewRaysCache_.size() == colorBuffer_.size();

    if (cacheCompatible) {
        return;
    }

    skyboxViewRayCacheWidth_ = width_;
    skyboxViewRayCacheHeight_ = height_;
    skyboxViewRayCacheFovYDeg_ = camera.fovYDeg();
    skyboxViewRayCacheAspect_ = camera.aspectRatio();
    skyboxViewRaysCache_.assign(colorBuffer_.size(), glm::vec3(0.0f, 0.0f, 1.0f));

    const float tanHalfFovY = std::tan(glm::radians(camera.fovYDeg()) * 0.5f);
    const float tanHalfFovX = tanHalfFovY * camera.aspectRatio();

    // 仅在分辨率/FOV/宽高比变化时重建每像素视线，避免每帧重复归一化开销。
    for (int y = 0; y < height_; ++y) {
        const float screenY = 1.0f - 2.0f * ((static_cast<float>(y) + 0.5f) / static_cast<float>(height_));
        for (int x = 0; x < width_; ++x) {
            const float screenX = 2.0f * ((static_cast<float>(x) + 0.5f) / static_cast<float>(width_)) - 1.0f;
            const glm::vec3 localRay = {
                screenX * tanHalfFovX,
                screenY * tanHalfFovY,
                1.0f};
            skyboxViewRaysCache_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = NormalizeVectorSafe(localRay, glm::vec3(0.0f, 0.0f, 1.0f));
        }
    }
}

void SoftwareRenderer::drawSkyboxBackground(const Scene& scene, const std::vector<float>* depthBuffer)
{
    if (!scene.camera || !scene.skyboxTexture || !scene.skyboxTexture->valid()) {
        return;
    }

    updateSkyboxViewRayCache(*scene.camera);

    const glm::vec3 cameraForward = NormalizeVectorSafe(
        scene.camera->target() - scene.camera->position(),
        glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 cameraUp = NormalizeVectorSafe(scene.camera->up(), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 cameraRight = NormalizeVectorSafe(glm::cross(cameraForward, cameraUp), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraOrthoUp = NormalizeVectorSafe(glm::cross(cameraRight, cameraForward), glm::vec3(0.0f, 1.0f, 0.0f));

    SkyboxDrawContext context;
    context.colorBuffer = &colorBuffer_;
    context.skyboxTexture = scene.skyboxTexture.get();
    context.localViewRays = &skyboxViewRaysCache_;
    context.depthBuffer = depthBuffer;
    context.cameraForward = cameraForward;
    context.cameraRight = cameraRight;
    context.cameraOrthoUp = cameraOrthoUp;
    context.hasDepthMask = depthBuffer != nullptr && depthBuffer->size() == colorBuffer_.size();

    const std::size_t totalPixels = colorBuffer_.size();
    const std::size_t workerCount = std::max<std::size_t>(1, fragmentShadingThreadPool_.threadCount());
    const bool enableParallelSkybox = fragmentMultithreadingEnabled_
        && workerCount > 1
        && totalPixels >= 4096;

    if (!enableParallelSkybox) {
        DrawSkyboxPixelRange(context, 0, totalPixels);
        return;
    }

    const std::size_t minChunkSize = std::max<std::size_t>(static_cast<std::size_t>(256), totalPixels / (workerCount * 8));
    fragmentShadingThreadPool_.parallelFor(
        0,
        totalPixels,
        minChunkSize,
        std::bind(
            DrawSkyboxPixelRange,
            std::cref(context),
            std::placeholders::_1,
            std::placeholders::_2));
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

        if (mainLight->type() == Light::LightType::Point) {
            // 点光源阴影使用 6 面深度图，每个面独立投影与写入深度。按 shadowMapResolution 分配每面缓存，并对 6 个面循环执行阴影 pass。
            const int pointShadowResolution = std::max(scene.shadowSettings.shadowMapResolution, 16);
            mainLight->setPointShadowResolution(pointShadowResolution);

            // 在并行前缓存会投射阴影的对象及其模型矩阵，避免多线程中重复读取可变缓存状态。
            std::vector<PointShadowCaster> shadowCasters;
            shadowCasters.reserve(scene.objects.size());
            for (const auto& obj : scene.objects) {
                if (!obj || !obj->castShadow()) {
                    continue;
                }

                PointShadowCaster caster;
                caster.object = obj.get();
                caster.modelMatrix = obj->modelMatrix();
                shadowCasters.push_back(caster);
            }

            for (std::size_t face = 0; face < Light::kPointShadowFaceCount; ++face) {
                std::vector<float>& faceDepths = mainLight->pointLightViewDepths(face);
                const size_t requiredSize = static_cast<size_t>(pointShadowResolution) * static_cast<size_t>(pointShadowResolution);
                if (faceDepths.size() != requiredSize) {
                    faceDepths.assign(requiredSize, std::numeric_limits<float>::infinity());
                } else {
                    std::fill(faceDepths.begin(), faceDepths.end(), std::numeric_limits<float>::infinity());
                }
            }

            if (!shadowCasters.empty()) {
                const std::size_t faceCount = Light::kPointShadowFaceCount;
                const std::size_t workerCount = std::max<std::size_t>(1, fragmentShadingThreadPool_.threadCount());
                const bool enableParallelPointShadow = fragmentMultithreadingEnabled_ && workerCount > 1;

                if (!enableParallelPointShadow) {
                    RasterizePointShadowFaceRange(*mainLight, shadowCasters, pointShadowResolution, 0, faceCount);
                } else {
                    fragmentShadingThreadPool_.parallelFor(
                        0,
                        faceCount,
                        1,
                        std::bind(
                            RasterizePointShadowFaceRange,
                            std::ref(*mainLight),
                            std::cref(shadowCasters),
                            pointShadowResolution,
                            std::placeholders::_1,
                            std::placeholders::_2));
                }
            }
        } else {
            // 方向光路径继续使用单张 2D 深度图，与已有阴影流程兼容。按当前渲染分辨率分配缓存，使用 lightSpaceMatrix 做一次阴影 pass。
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
            for (const auto& obj : scene.objects) {
                if (!obj || !obj->castShadow()) {
                    continue;
                }

                const glm::mat4 model = obj->modelMatrix();
                const glm::mat4 mvp = lightMatrix * model;
                RasterizeObjectShadowTriangles(obj.get(), mvp, shadowWidth, shadowHeight, lightDepths);
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
        const std::weak_ptr<Material> objectMaterial = obj->material();

        if (const MeshObject* meshObject = dynamic_cast<MeshObject*>(obj.get())) {
            if (!meshObject->hasMesh()) {
                continue;
            }

            const ObjMeshData& mesh = meshObject->mesh();

            for(auto & face : mesh.indices)
            {
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
                rasterizeLocalTriangle(model, mvp, localVertices, objectTexture, objectMaterial);
            }
        } else if (const Triangle* tri = dynamic_cast<Triangle*>(obj.get())) {
            std::array<Vertex, 3> localVertices;
            for (int i = 0; i < 3; ++i) {
                localVertices[i].position = glm::vec3(tri->getVertexs()[i]);
                localVertices[i].color = tri->getColors()[i];
                localVertices[i].texCoord = tri->getTexCoords()[i];
                localVertices[i].normal = tri->getNormal();
            }
            rasterizeLocalTriangle(model, mvp, localVertices, objectTexture, objectMaterial);
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
                rasterizeLocalTriangle(model, mvp, localVertices, objectTexture, objectMaterial);
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
                rasterizeLocalTriangle(model, mvp, localVertices, objectTexture, objectMaterial);
            }
        }
    }

    if (scene.enableSkybox && scene.skyboxTexture && scene.skyboxTexture->valid()) {
        // 在几何光栅化后仅补全未命中前景的背景像素，减少天空盒路径的无效采样。
        drawSkyboxBackground(scene, &rasterizer_.zBuffer());
    }

    //Fragment Shader：遍历光栅化阶段生成的片段，进行深度测试和颜色写入。
    // for(const auto& frag : rasterizer_.fragments())
    // {
    //     if (fragmentShader_) {
    //         fragmentShader_(colorBuffer_, frag, scene);
    //     } else {
    //         putPixel(frag.bufferIndex, frag.color);
    //     }
    // }

    // 使用线程池并行执行片元着色，避免每帧创建/销毁线程带来的额外开销。
    const auto& fragments = rasterizer_.fragments();
    const std::size_t totalFragments = fragments.size();

    fragmentThreadingStats_.fragmentCount = totalFragments;
    fragmentThreadingStats_.poolThreadCount = fragmentShadingThreadPool_.threadCount();
    fragmentThreadingStats_.activeWorkerCount = fragmentShadingThreadPool_.activeWorkerCount();
    fragmentThreadingStats_.pendingTaskCount = fragmentShadingThreadPool_.pendingTaskCount();
    fragmentThreadingStats_.scheduledTaskCount = 0;
    fragmentThreadingStats_.dispatchedWorkerCount = 0;

    if (totalFragments == 0) {
        return;
    }

    FragmentShadingContext context;
    context.fragments = &fragments;
    context.colorBuffer = &colorBuffer_;
    context.fragmentShader = &fragmentShader_;
    context.scene = &scene;

    // 关闭多线程时走单线程路径，便于在 DebugUI 中对比效率。
    if (!fragmentMultithreadingEnabled_) {
        fragmentThreadingStats_.scheduledTaskCount = 1;
        fragmentThreadingStats_.dispatchedWorkerCount = 1;
        ShadeFragmentRange(context, 0, totalFragments);
        fragmentThreadingStats_.activeWorkerCount = 0;
        fragmentThreadingStats_.pendingTaskCount = 0;
        return;
    }

    const std::size_t workerCount = std::max<std::size_t>(1, fragmentShadingThreadPool_.threadCount());
    const std::size_t minChunkSize = std::max<std::size_t>(static_cast<std::size_t>(256), totalFragments / (workerCount * 4));
    const std::size_t taskCount = (totalFragments + minChunkSize - 1) / minChunkSize;

    fragmentThreadingStats_.scheduledTaskCount = taskCount;
    fragmentThreadingStats_.dispatchedWorkerCount = std::min(workerCount, taskCount);

    fragmentShadingThreadPool_.parallelFor(
        0,
        totalFragments,
        minChunkSize,
        std::bind(
            ShadeFragmentRange,
            std::cref(context),
            std::placeholders::_1,
            std::placeholders::_2));

    // 任务提交并等待完成后，刷新一次线程池即时状态用于 UI 展示。
    fragmentThreadingStats_.activeWorkerCount = fragmentShadingThreadPool_.activeWorkerCount();
    fragmentThreadingStats_.pendingTaskCount = fragmentShadingThreadPool_.pendingTaskCount();
}
