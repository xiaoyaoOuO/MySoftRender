#pragma once
#include "Triangle.h"
#include "ObjLoader.h"
#include "Texture.h"
#include "Scene.h"
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
    glm::vec3 worldPos; // 世界空间位置
    float shadowVisibility = 1.0f; // 阴影可见性（1=全亮，0=全阴影）
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

namespace {
// 定义采样缓冲的最大展开采样数（用于固定步长索引）。当前支持 1/2/4 三档 MSAA，底层缓冲按最大 4 采样预分配。
constexpr int kMaxMsaaSamples = 4;

size_t BufferIndex(int x, int y, int width) {
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

// 定义 1x/2x/4x 对应的子采样点偏移。根据当前 MSAA 档位选择对应偏移数组参与覆盖测试。
constexpr std::array<Vec2, 1> kMsaaOffsets1 = {
    Vec2{0.5f, 0.5f}
};
constexpr std::array<Vec2, 2> kMsaaOffsets2 = {
    Vec2{0.25f, 0.5f},
    Vec2{0.75f, 0.5f}
};
constexpr std::array<Vec2, kMaxMsaaSamples> kMsaaOffsets4 = {
    Vec2{0.25f, 0.25f},
    Vec2{0.75f, 0.25f},
    Vec2{0.25f, 0.75f},
    Vec2{0.75f, 0.75f}
};

// 将传入采样数规范到 1/2/4 三档。用户传入任意整数时，统一映射到最近合法档位，避免非法参数破坏光栅流程。
int NormalizeMsaaSampleCount(int sampleCount)
{
    if (sampleCount <= 1) {
        return 1;
    }
    if (sampleCount <= 2) {
        return 2;
    }
    return 4;
}

// 根据采样档位返回对应子采样偏移数组首地址。配合 activeSampleCount 一起传入光栅主循环。
const Vec2* ResolveMsaaOffsets(int activeSampleCount)
{
    if (activeSampleCount == 1) {
        return kMsaaOffsets1.data();
    }
    if (activeSampleCount == 2) {
        return kMsaaOffsets2.data();
    }
    return kMsaaOffsets4.data();
}

// 把像素索引与子采样索引映射到 sample 缓冲的一维下标。访问 sampleZBuffer、sampleColorBuffer、sampleNormalBuffer 时统一调用，sampleIndex 范围为 [0, kMaxMsaaSamples)。
size_t SampleBufferIndex(size_t pixelIndex, int sampleIndex)
{
    return pixelIndex * static_cast<size_t>(kMaxMsaaSamples) + static_cast<size_t>(sampleIndex);
}

Color ToColor(const glm::vec3& color)
{
    const glm::vec3 clamped = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    return Color{
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(clamped.r * 255.0f), 0, 255)),
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(clamped.g * 255.0f), 0, 255)),
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(clamped.b * 255.0f), 0, 255)),
        255
    };
}

// 将一个像素对应的 4 个 sample 深度解析为最终像素深度。在像素完成 sample 级深度写入后调用，返回最靠前（最小）的深度，用于更新像素级 zBuffer。
float ResolvePixelDepth(size_t pixelIndex, const std::vector<float>& sampleZBuffer, int activeSampleCount)
{
    float minDepth = std::numeric_limits<float>::infinity();
    for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
        minDepth = std::min(minDepth, sampleZBuffer[SampleBufferIndex(pixelIndex, sampleIndex)]);
    }
    return minDepth;
}

// 将一个像素的 4 个 sample 颜色做平均，得到最终像素颜色。在通过深度测试的 sample 写入颜色后调用，结果再通过 ToColor 转为 8-bit RGBA 输出。
glm::vec3 ResolvePixelColor(size_t pixelIndex, const std::vector<glm::vec3>& sampleColorBuffer, int activeSampleCount)
{
    glm::vec3 accumulated(0.0f);
    for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
        accumulated += sampleColorBuffer[SampleBufferIndex(pixelIndex, sampleIndex)];
    }
    return accumulated * (1.0f / static_cast<float>(activeSampleCount));
}

// 从与解析深度匹配的 sample 中恢复像素法线，供后续调试显示或可视化使用。先传入 ResolvePixelDepth 的结果，再从 sampleNormalBuffer 中挑选深度最接近的法线返回。
glm::vec3 ResolvePixelNormal(
    size_t pixelIndex,
    float resolvedDepth,
    const std::vector<float>& sampleZBuffer,
    const std::vector<glm::vec3>& sampleNormalBuffer,
    int activeSampleCount)
{
    static constexpr float kDepthEpsilon = 1e-6f;
    for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
        const size_t index = SampleBufferIndex(pixelIndex, sampleIndex);
        if (std::abs(sampleZBuffer[index] - resolvedDepth) <= kDepthEpsilon) {
            return sampleNormalBuffer[index];
        }
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

// 将屏幕空间重心坐标修正为透视正确重心坐标。传入线性重心坐标和每个顶点 invW，返回可用于 worldPos/UV/法线插值的修正权重。
std::array<float, 3> PerspectiveCorrectWeights(
    float w0,
    float w1,
    float w2,
    const std::array<float, 3>& vertexInvW)
{
    const float p0 = w0 * vertexInvW[0];
    const float p1 = w1 * vertexInvW[1];
    const float p2 = w2 * vertexInvW[2];
    const float denom = p0 + p1 + p2;

    if (std::abs(denom) <= 1e-8f) {
        return {w0, w1, w2};
    }

    const float invDenom = 1.0f / denom;
    return {p0 * invDenom, p1 * invDenom, p2 * invDenom};
}

/**
 * @brief 根据光源到片元的方向向量选择点光阴影采样面。
 * @param lightToFragment 从光源位置指向片元位置的方向向量。
 * @return 返回 0~5 的面索引（+X/-X/+Y/-Y/+Z/-Z）。
 */
int SelectPointShadowFace(const glm::vec3& lightToFragment)
{
    const float absX = std::abs(lightToFragment.x);
    const float absY = std::abs(lightToFragment.y);
    const float absZ = std::abs(lightToFragment.z);

    if (absX >= absY && absX >= absZ) {
        return (lightToFragment.x >= 0.0f) ? 0 : 1;
    }
    if (absY >= absX && absY >= absZ) {
        return (lightToFragment.y >= 0.0f) ? 2 : 3;
    }
    return (lightToFragment.z >= 0.0f) ? 4 : 5;
}

/**
 * @brief 将世界坐标投影到阴影贴图像素坐标并返回接收者深度。
 * @param lightSpaceMatrix 光源空间矩阵。
 * @param worldPos 片元世界坐标。
 * @param shadowWidth 阴影贴图宽度。
 * @param shadowHeight 阴影贴图高度。
 * @param outShadowX 输出阴影贴图像素 x 坐标。
 * @param outShadowY 输出阴影贴图像素 y 坐标。
 * @param outReceiverDepth 输出接收者深度（0~1）。
 * @return 投影成功返回 true，失败返回 false。
 */
bool ProjectWorldPosToShadowTexel(
    const glm::mat4& lightSpaceMatrix,
    const glm::vec3& worldPos,
    int shadowWidth,
    int shadowHeight,
    int& outShadowX,
    int& outShadowY,
    float& outReceiverDepth)
{
    if (shadowWidth <= 0 || shadowHeight <= 0) {
        return false;
    }

    glm::vec4 lightSpacePos = lightSpaceMatrix * glm::vec4(worldPos, 1.0f);
    if (std::abs(lightSpacePos.w) <= 1e-6f) {
        return false;
    }
    lightSpacePos /= lightSpacePos.w;

    const bool outsideLightFrustum =
        (lightSpacePos.x < -1.0f || lightSpacePos.x > 1.0f)
        || (lightSpacePos.y < -1.0f || lightSpacePos.y > 1.0f)
        || (lightSpacePos.z < -1.0f || lightSpacePos.z > 1.0f);
    if (outsideLightFrustum) {
        return false;
    }

    const int shadowX = static_cast<int>((lightSpacePos.x + 1.0f) * 0.5f * static_cast<float>(shadowWidth));
    const int shadowY = static_cast<int>((1.0f - (lightSpacePos.y + 1.0f) * 0.5f) * static_cast<float>(shadowHeight));
    if (shadowX < 0 || shadowX >= shadowWidth || shadowY < 0 || shadowY >= shadowHeight) {
        return false;
    }

    outShadowX = shadowX;
    outShadowY = shadowY;
    outReceiverDepth = (lightSpacePos.z + 1.0f) * 0.5f;
    return true;
}

/**
 * @brief 使用硬阴影深度比较计算可见性。
 * @param shadowMap 深度贴图数据。
 * @param shadowWidth 深度贴图宽度。
 * @param shadowHeight 深度贴图高度。
 * @param shadowX 采样像素 x 坐标。
 * @param shadowY 采样像素 y 坐标。
 * @param receiverDepth 当前接收者深度（0~1）。
 * @param bias 阴影偏移值。
 * @param shadowSettings 场景阴影配置，供后续软阴影实现使用。
 * @return 可见性（1.0 表示受光，0.0 表示被遮挡）。
 */
float ComputeShadowVisibilityFromDepthMap(
    const std::vector<float>& shadowMap,
    int shadowWidth,
    int shadowHeight,
    int shadowX,
    int shadowY,
    float receiverDepth,
    float bias,
    const ShadowSettings& shadowSettings)
{
    if (shadowWidth <= 0 || shadowHeight <= 0) {
        return 1.0f;
    }

    const std::size_t requiredSize = static_cast<std::size_t>(shadowWidth) * static_cast<std::size_t>(shadowHeight);
    if (shadowMap.size() < requiredSize) {
        return 1.0f;
    }

    if (shadowX < 0 || shadowX >= shadowWidth || shadowY < 0 || shadowY >= shadowHeight) {
        return 1.0f;
    }

    if (shadowSettings.filterMode == ShadowFilterMode::PCF) {
        // PCF 采样时固定统计核内样本数量，避免边界样本导致分子/分母不一致。越界坐标做钳制采样，抑制阴影图边缘的异常亮线。
        const int kernelRadius = std::max(shadowSettings.pcfKernelRadius, 1);
        int sampleCount = 0;
        float visibilitySum = 0.0f;
        const float compareDepth = receiverDepth - bias;

        for (int offsetY = -kernelRadius; offsetY <= kernelRadius; ++offsetY) {
            for (int offsetX = -kernelRadius; offsetX <= kernelRadius; ++offsetX) {
                const int sampleX = std::clamp(shadowX + offsetX, 0, shadowWidth - 1);
                const int sampleY = std::clamp(shadowY + offsetY, 0, shadowHeight - 1);
                const std::size_t sampleIndex = BufferIndex(sampleX, sampleY, shadowWidth);
                const float blockerDepth = shadowMap[sampleIndex];
                visibilitySum += (compareDepth > blockerDepth) ? 0.0f : 1.0f;
                ++sampleCount;
            }
        }

        if (sampleCount <= 0) {
            return 1.0f;
        }
        const float visibility = visibilitySum / static_cast<float>(sampleCount);
        return std::clamp(visibility, 0.0f, 1.0f);
    }

    const std::size_t shadowIndex = BufferIndex(shadowX, shadowY, shadowWidth);
    const float blockerDepth = shadowMap[shadowIndex];
    const float visibility = (receiverDepth - bias > blockerDepth) ? 0.0f : 1.0f;
    return std::clamp(visibility, 0.0f, 1.0f);
}

/**
 * @brief 计算点光源路径下的硬阴影可见性。
 * @param scene 当前场景对象，读取阴影参数。
 * @param light 当前光源对象。
 * @param worldPos 片元世界坐标。
 * @return 可见性（1.0 表示受光，0.0 表示被遮挡）。
 */
float ComputePointShadowVisibility(const Scene& scene, const Light& light, const glm::vec3& worldPos)
{
    const glm::vec3 lightToFragment = worldPos - light.position();
    if (glm::dot(lightToFragment, lightToFragment) <= 1e-12f) {
        return 1.0f;
    }

    const int faceIndex = SelectPointShadowFace(lightToFragment);
    const int shadowResolution = light.pointShadowResolution();
    const std::vector<float>& shadowMap = light.pointLightViewDepths(static_cast<std::size_t>(faceIndex));
    const size_t expectedSize = static_cast<size_t>(shadowResolution) * static_cast<size_t>(shadowResolution);
    if (shadowResolution <= 0 || shadowMap.size() != expectedSize) {
        return 1.0f;
    }

    int shadowX = 0;
    int shadowY = 0;
    float receiverDepth = 0.0f;

    if (!ProjectWorldPosToShadowTexel(
            light.pointLightSpaceMatrix(static_cast<std::size_t>(faceIndex)),
            worldPos,
            shadowResolution,
            shadowResolution,
            shadowX,
            shadowY,
            receiverDepth)) {
        return 1.0f;
    }

    const float bias = std::max(scene.shadowSettings.depthBias, 0.0f);
    return ComputeShadowVisibilityFromDepthMap(
        shadowMap,
        shadowResolution,
        shadowResolution,
        shadowX,
        shadowY,
        receiverDepth,
        bias,
        scene.shadowSettings
    );
}

/**
 * @brief 计算方向光/聚光路径下的硬阴影可见性。
 * @param scene 当前场景对象，读取阴影参数。
 * @param light 当前光源对象。
 * @param worldPos 片元世界坐标。
 * @return 可见性（1.0 表示受光，0.0 表示被遮挡）。
 */
float ComputeDirectionalShadowVisibility(const Scene& scene, const Light& light, const glm::vec3& worldPos)
{
    const int shadowWidth = light.shadowMapWidth();
    const int shadowHeight = light.shadowMapHeight();
    const std::vector<float>& shadowMap = light.lightViewDepths();
    const size_t expectedSize = static_cast<std::size_t>(shadowWidth) * static_cast<std::size_t>(shadowHeight);
    if (shadowWidth <= 0 || shadowHeight <= 0 || shadowMap.size() != expectedSize) {
        return 1.0f;
    }

    int shadowX = 0;
    int shadowY = 0;
    float receiverDepth = 0.0f;
    if (!ProjectWorldPosToShadowTexel(
            light.lightSpaceMatrix(),
            worldPos,
            shadowWidth,
            shadowHeight,
            shadowX,
            shadowY,
            receiverDepth)) {
        return 1.0f;
    }

    const float bias = std::max(scene.shadowSettings.depthBias, 0.0f);
    return ComputeShadowVisibilityFromDepthMap(
        shadowMap,
        shadowWidth,
        shadowHeight,
        shadowX,
        shadowY,
        receiverDepth,
        bias,
        scene.shadowSettings
    );
}

/**
 * @brief 统一计算片元阴影可见性，内部按光源类型分发。
 * @param scene 当前场景对象。
 * @param light 当前光源对象。
 * @param worldPos 片元世界坐标。
 * @return 可见性（1.0 表示受光，0.0 表示被遮挡）。
 */
float ComputeShadowVisibility(const Scene& scene, const Light& light, const glm::vec3& worldPos)
{
    if (light.type() == Light::LightType::Point) {
        return ComputePointShadowVisibility(scene, light, worldPos);
    }
    return ComputeDirectionalShadowVisibility(scene, light, worldPos);
}

/**
 * @brief 向线框调试片元缓存尝试写入一个像素，包含深度测试与属性填充。
 * @param x 待写入像素 x 坐标。
 * @param y 待写入像素 y 坐标。
 * @param t 线段插值参数（0 表示起点，1 表示终点）。
 * @param v0 线段起点（屏幕空间，z 为深度）。
 * @param v1 线段终点（屏幕空间，z 为深度）。
 * @param normal 线框片元法线（用于保持调试输出结构一致）。
 * @param width 当前渲染目标宽度（像素）。
 * @param height 当前渲染目标高度（像素）。
 * @param zBuffer 像素级深度缓存。
 * @param fragments 片元输出数组，函数会在通过深度测试后写入。
 * @return 无返回值。
 */
void TryEmitWireframeFragment(
    int x,
    int y,
    float t,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& normal,
    int width,
    int height,
    std::vector<float>& zBuffer,
    std::vector<Fragment>& fragments)
{
    static constexpr float kEdgeDepthBias = 1e-4f;
    static constexpr Color kEdgeColor = {255, 255, 255, 255};

    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }

    const float rawDepth = v0.z + (v1.z - v0.z) * t;
    const float depth = std::clamp(rawDepth - kEdgeDepthBias, 0.0f, 1.0f);
    const size_t bufferIndex = BufferIndex(x, y, width);
    if (depth >= zBuffer[bufferIndex]) {
        return;
    }

    zBuffer[bufferIndex] = depth;
    Fragment frag;
    frag.screenPos = Vec2{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
    frag.bufferIndex = bufferIndex;
    frag.depth = depth;
    frag.color = kEdgeColor;
    frag.normal = normal;
    frag.worldPos = glm::vec3(0.0f);
    fragments.push_back(frag);
}

void RasterizeLine(
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& normal,
    int width,
    int height,
    std::vector<float>& zBuffer,
    std::vector<Fragment>& fragments)
{
    const int x0 = static_cast<int>(std::round(v0.x));
    const int y0 = static_cast<int>(std::round(v0.y));
    const int x1 = static_cast<int>(std::round(v1.x));
    const int y1 = static_cast<int>(std::round(v1.y));

    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int steps = std::max(dx, dy);

    if (steps == 0) {
        TryEmitWireframeFragment(x0, y0, 0.0f, v0, v1, normal, width, height, zBuffer, fragments);
        return;
    }

    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const float xf = static_cast<float>(x0) + static_cast<float>(x1 - x0) * t;
        const float yf = static_cast<float>(y0) + static_cast<float>(y1 - y0) * t;
        const int x = static_cast<int>(std::round(xf));
        const int y = static_cast<int>(std::round(yf));
        TryEmitWireframeFragment(x, y, t, v0, v1, normal, width, height, zBuffer, fragments);
    }
}

// 封装三角形像素着色逻辑，统一处理“纯顶点色”与“纹理*顶点色”两条路径。
// 参数：
// - vertices：当前三角形三个顶点，提供插值所需的顶点颜色。
// - seamSafeTexCoords：已做接缝修正的 UV，避免跨 0/1 边界时插值跳变。
// - texture：纹理指针，若为空则走纯顶点色路径。
// 作为 RasterizeTriangleMSAA 的具名回调对象，替代函数内部 Lambda。
struct ShadePixelWithTexture
{
    const std::array<Vertex, 3>& vertices;
    const std::array<glm::vec2, 3>& seamSafeTexCoords;
    const Texture2D* texture = nullptr;

    glm::vec3 operator()(float w0, float w1, float w2) const
    {
        const glm::vec3 vertexColor = vertices[0].color * w0 + vertices[1].color * w1 + vertices[2].color * w2;
        if (!texture) {
            return vertexColor;
        }

        float u = seamSafeTexCoords[0].x * w0 + seamSafeTexCoords[1].x * w1 + seamSafeTexCoords[2].x * w2;
        u -= std::floor(u);
        const float v = std::clamp(
            seamSafeTexCoords[0].y * w0 + seamSafeTexCoords[1].y * w1 + seamSafeTexCoords[2].y * w2,
            0.0f,
            1.0f);

        return texture->sample(u, v) * vertexColor;
    }
};
}

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

    // 只读访问像素级深度缓冲。用于后处理阶段判断像素是否已被几何体覆盖。
    const std::vector<float>& zBuffer() const { return zBuffer_; }

    // 查询是否启用背面剔除。返回 true 时，仅保留当前约定的正面三角形参与光栅化。
    bool backfaceCullingEnabled() const { return backfaceCullingEnabled_; }

    // 启用或关闭背面剔除。传入 true 开启，false 关闭，可在运行时动态切换观察效果与性能。
    void setBackfaceCullingEnabled(bool enabled) { backfaceCullingEnabled_ = enabled; }

    // 切换背面剔除状态。通常绑定热键快速比较剔除开关对帧率和画面的影响。
    void toggleBackfaceCulling() { backfaceCullingEnabled_ = !backfaceCullingEnabled_; }

    // 获取当前 MSAA 采样数档位。返回值仅为 1/2/4，用于 UI 展示和调试输出。
    int msaaSampleCount() const { return msaaSampleCount_; }

    // 设置 MSAA 采样数档位。支持 1/2/4，传入其它值会自动归一到最近合法档位。
    void setMsaaSampleCount(int sampleCount);

    bool wireframeOverlayEnabled() const { return wireframeOverlayEnabled_; }
    void setWireframeOverlayEnabled(bool enabled) { wireframeOverlayEnabled_ = enabled; }
    void toggleWireframeOverlay() { wireframeOverlayEnabled_ = !wireframeOverlayEnabled_; }

    // 清除帧缓冲和所有相关的状态（如 ZBuffer，片段缓存等）
    void Clear();

    // 将三角形执行光栅化并生成可供片段着色器使用的片段数据。除屏幕空间 vertices 外，可选传入世界空间位置/法线用于光照计算；texture 为空时走纯色路径。
    void Rasterize_Triangle(
        const std::array<Vertex, 3>& vertices,
        const Texture2D* texture = nullptr,
        const std::array<glm::vec3, 3>* worldPositions = nullptr,
        const std::array<glm::vec3, 3>* worldNormals = nullptr);

    // 执行三角形的 MSAA 光栅化，完成 sample 级覆盖测试、深度测试与像素级 resolve。由 Rasterizer::Rasterize_Triangle 传入缓冲区与 shadePixel 回调；回调接收重心坐标 (w0, w1, w2) 并返回该像素的线性空间颜色。
    template <typename ShadePixelFunc>
    void RasterizeTriangleMSAA(
        const std::array<glm::vec3, 3>& vertexs,
        const std::array<float, 3>& vertexInvW,
        const std::array<glm::vec3, 3>* worldPositions,
        const std::array<glm::vec3, 3>* worldNormals,
        int activeSampleCount,
        const Vec2* sampleOffsets,
        ShadePixelFunc&& shadePixel);

private:
    static constexpr int kMsaaSampleCount = 4;

    int width_;
    int height_;
    bool backfaceCullingEnabled_ = true;
    bool wireframeOverlayEnabled_ = false;
    int msaaSampleCount_ = 1;

    std::mutex zBufferMutex_; // 保护 zBuffer_ 和 sampleZBuffer_ 的互斥锁，确保多线程写入安全
    std::vector<float> zBuffer_; // 深度缓冲
    std::vector<float> sampleZBuffer_; // 每像素每采样点深度缓冲

    std::mutex colorBufferMutex_; // 保护 sampleColorBuffer_ 和 sampleNormalBuffer_ 的互斥锁，确保多线程写入安全
    std::vector<glm::vec3> sampleColorBuffer_; // 每像素每采样点颜色
    std::vector<glm::vec3> sampleNormalBuffer_; // 每像素每采样点法线

    std::mutex fragmentLutMutex_; // 保护 fragmentLut_ 和 fragments_ 的互斥锁，确保多线程写入安全
    std::vector<int> fragmentLut_; // 像素索引到 fragments_ 下标映射
    std::vector<Fragment> fragments_; // 光栅化阶段生成的片段列表
};


template <typename ShadePixelFunc>
void Rasterizer::RasterizeTriangleMSAA(
    const std::array<glm::vec3, 3>& vertexs,
    const std::array<float, 3>& vertexInvW,
    const std::array<glm::vec3, 3>* worldPositions,
    const std::array<glm::vec3, 3>* worldNormals,
    int activeSampleCount,
    const Vec2* sampleOffsets,
    ShadePixelFunc&& shadePixel)
{
    int minX = static_cast<int>(std::floor(std::min({vertexs[0].x, vertexs[1].x, vertexs[2].x})));
    int maxX = static_cast<int>(std::ceil(std::max({vertexs[0].x, vertexs[1].x, vertexs[2].x})));
    int minY = static_cast<int>(std::floor(std::min({vertexs[0].y, vertexs[1].y, vertexs[2].y})));
    int maxY = static_cast<int>(std::ceil(std::max({vertexs[0].y, vertexs[1].y, vertexs[2].y})));

    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, width_ - 1);
    maxY = std::min(maxY, height_ - 1);

    if (minX > maxX || minY > maxY) {
        return;
    }

    const Vec2 v0 = {vertexs[0].x, vertexs[0].y};
    const Vec2 v1 = {vertexs[1].x, vertexs[1].y};
    const Vec2 v2 = {vertexs[2].x, vertexs[2].y};

    const float area = VectorMath::edgeFunction(v0, v1, v2);
    if (std::abs(area) <= 1e-6f) {
        return;
    }

    const bool isAreaPositive = area > 0.0f;
    const float invArea = 1.0f / area;

    glm::vec3 triangleNormal = VectorMath::getNormal(vertexs[0], vertexs[1], vertexs[2]);
    if (worldPositions != nullptr) {
        triangleNormal = VectorMath::getNormal((*worldPositions)[0], (*worldPositions)[1], (*worldPositions)[2]);
    }

    const float z0 = vertexs[0].z;
    const float z1 = vertexs[1].z;
    const float z2 = vertexs[2].z;

    for (int j = minY; j <= maxY; ++j) {
        for (int i = minX; i <= maxX; ++i) {
            const size_t pixelIndex = BufferIndex(i, j, width_);
            std::array<bool, kMaxMsaaSamples> samplePassed = {false, false, false, false};
            int passedSampleCount = 0;
            float centroidX = 0.0f;
            float centroidY = 0.0f;

            for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
                const Vec2 samplePoint = {
                    static_cast<float>(i) + sampleOffsets[sampleIndex].x,
                    static_cast<float>(j) + sampleOffsets[sampleIndex].y
                };

                float w0 = VectorMath::edgeFunction(v1, v2, samplePoint);
                float w1 = VectorMath::edgeFunction(v2, v0, samplePoint);
                float w2 = VectorMath::edgeFunction(v0, v1, samplePoint);
                const bool inside = isAreaPositive
                    ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                    : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);

                if (!inside) {
                    continue;
                }

                w0 *= invArea;
                w1 *= invArea;
                w2 *= invArea;

                const float depth = z0 * w0 + z1 * w1 + z2 * w2;
                const size_t sampleBufferIndex = SampleBufferIndex(pixelIndex, sampleIndex);
                
                if (depth >= sampleZBuffer_[sampleBufferIndex]) {
                    continue;
                }

                sampleZBuffer_[sampleBufferIndex] = depth;
                sampleNormalBuffer_[sampleBufferIndex] = triangleNormal;
                samplePassed[sampleIndex] = true;
                ++passedSampleCount;
                centroidX += samplePoint.x;
                centroidY += samplePoint.y;
            }

            if (passedSampleCount == 0) {
                continue;
            }

            Vec2 shadePoint{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f};
            if (passedSampleCount != activeSampleCount) {
                shadePoint.x = centroidX / static_cast<float>(passedSampleCount);
                shadePoint.y = centroidY / static_cast<float>(passedSampleCount);
            }

            float shadeW0 = VectorMath::edgeFunction(v1, v2, shadePoint) * invArea;
            float shadeW1 = VectorMath::edgeFunction(v2, v0, shadePoint) * invArea;
            float shadeW2 = VectorMath::edgeFunction(v0, v1, shadePoint) * invArea;
            const std::array<float, 3> correctedWeights = PerspectiveCorrectWeights(
                shadeW0,
                shadeW1,
                shadeW2,
                vertexInvW);
            const float correctedW0 = correctedWeights[0];
            const float correctedW1 = correctedWeights[1];
            const float correctedW2 = correctedWeights[2];

            const glm::vec3 pixelShadedColor = glm::clamp(
                shadePixel(correctedW0, correctedW1, correctedW2),
                glm::vec3(0.0f),
                glm::vec3(1.0f));

            
            for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
                if (!samplePassed[sampleIndex]) {
                    continue;
                }
                const size_t sampleBufferIndex = SampleBufferIndex(pixelIndex, sampleIndex);
                sampleColorBuffer_[sampleBufferIndex] = pixelShadedColor;
            }

            const float resolvedDepth = ResolvePixelDepth(pixelIndex, sampleZBuffer_, activeSampleCount);
            if (!std::isfinite(resolvedDepth)) {
                continue;
            }
            
            zBuffer_[pixelIndex] = resolvedDepth;

            Fragment frag;
            frag.screenPos = Vec2{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f};
            frag.bufferIndex = pixelIndex;
            frag.depth = resolvedDepth;
            frag.color = ToColor(ResolvePixelColor(pixelIndex, sampleColorBuffer_, activeSampleCount));

            // 根据光源阴影贴图判断当前片段是否被遮挡。将阴影采样逻辑提取到独立函数，减少主光栅化流程中的 if 嵌套层级。
            if (worldPositions != nullptr && Scene::instance) {
                const Scene& scene = *(Scene::instance);
                if (scene.shadowSettings.enableShadowMap && !scene.lights.empty()) {
                    const Light& light = *scene.lights[0];
                    const glm::vec3 worldPos = (*worldPositions)[0] * correctedW0
                        + (*worldPositions)[1] * correctedW1
                        + (*worldPositions)[2] * correctedW2;
                    frag.shadowVisibility = ComputeShadowVisibility(scene, light, worldPos);
                }
            }

            // 将当前像素重心坐标对应的世界空间位置与法线写入 Fragment，供光照在世界坐标系计算。若调用方提供了 worldPositions/worldNormals，则按重心插值；否则退回到已有法线解析路径。
            if (worldPositions != nullptr) {
                frag.worldPos = (*worldPositions)[0] * correctedW0
                    + (*worldPositions)[1] * correctedW1
                    + (*worldPositions)[2] * correctedW2;
            } else {
                frag.worldPos = glm::vec3(0.0f);
            }

            if (worldNormals != nullptr) {
                glm::vec3 interpolatedNormal = (*worldNormals)[0] * correctedW0
                    + (*worldNormals)[1] * correctedW1
                    + (*worldNormals)[2] * correctedW2;
                const float len2 = glm::dot(interpolatedNormal, interpolatedNormal);
                if (len2 > 1e-12f) {
                    frag.normal = glm::normalize(interpolatedNormal);
                } else {
                    frag.normal = triangleNormal;
                }
            } else {
                frag.normal = ResolvePixelNormal(pixelIndex, resolvedDepth, sampleZBuffer_, sampleNormalBuffer_, activeSampleCount);
            }

            const int fragmentIndex = fragmentLut_[pixelIndex];
            if (fragmentIndex >= 0) {
                fragments_[static_cast<std::size_t>(fragmentIndex)] = frag;
            } else {
                fragmentLut_[pixelIndex] = static_cast<int>(fragments_.size());
                fragments_.push_back(frag);
            }
        }
    }
}
