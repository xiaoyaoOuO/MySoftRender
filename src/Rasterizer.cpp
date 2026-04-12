#include "Rasterizer.h"
#include "Texture.h"
#include "Scene.h"

namespace {
// 作用：定义采样缓冲的最大展开采样数（用于固定步长索引）。
// 用法：当前支持 1/2/4 三档 MSAA，底层缓冲按最大 4 采样预分配。
constexpr int kMaxMsaaSamples = 4;

// 作用：定义 1x/2x/4x 对应的子采样点偏移。
// 用法：根据当前 MSAA 档位选择对应偏移数组参与覆盖测试。
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

// 作用：将传入采样数规范到 1/2/4 三档。
// 用法：用户传入任意整数时，统一映射到最近合法档位，避免非法参数破坏光栅流程。
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

// 作用：根据采样档位返回对应子采样偏移数组首地址。
// 用法：配合 activeSampleCount 一起传入光栅主循环。
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

// 作用：把像素索引与子采样索引映射到 sample 缓冲的一维下标。
// 用法：访问 sampleZBuffer、sampleColorBuffer、sampleNormalBuffer 时统一调用，sampleIndex 范围为 [0, kMaxMsaaSamples)。
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

// 作用：将一个像素对应的 4 个 sample 深度解析为最终像素深度。
// 用法：在像素完成 sample 级深度写入后调用，返回最靠前（最小）的深度，用于更新像素级 zBuffer。
float ResolvePixelDepth(size_t pixelIndex, const std::vector<float>& sampleZBuffer, int activeSampleCount)
{
    float minDepth = std::numeric_limits<float>::infinity();
    for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
        minDepth = std::min(minDepth, sampleZBuffer[SampleBufferIndex(pixelIndex, sampleIndex)]);
    }
    return minDepth;
}

// 作用：将一个像素的 4 个 sample 颜色做平均，得到最终像素颜色。
// 用法：在通过深度测试的 sample 写入颜色后调用，结果再通过 ToColor 转为 8-bit RGBA 输出。
glm::vec3 ResolvePixelColor(size_t pixelIndex, const std::vector<glm::vec3>& sampleColorBuffer, int activeSampleCount)
{
    glm::vec3 accumulated(0.0f);
    for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
        accumulated += sampleColorBuffer[SampleBufferIndex(pixelIndex, sampleIndex)];
    }
    return accumulated * (1.0f / static_cast<float>(activeSampleCount));
}

// 作用：从与解析深度匹配的 sample 中恢复像素法线，供后续调试显示或可视化使用。
// 用法：先传入 ResolvePixelDepth 的结果，再从 sampleNormalBuffer 中挑选深度最接近的法线返回。
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

// 作用：将屏幕空间重心坐标修正为透视正确重心坐标。
// 用法：传入线性重心坐标和每个顶点 invW，返回可用于 worldPos/UV/法线插值的修正权重。
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

// 作用：封装三角形像素着色逻辑，统一处理“纯顶点色”与“纹理*顶点色”两条路径。
// 参数：
// - vertices：当前三角形三个顶点，提供插值所需的顶点颜色。
// - seamSafeTexCoords：已做接缝修正的 UV，避免跨 0/1 边界时插值跳变。
// - texture：纹理指针，若为空则走纯顶点色路径。
// 用法：作为 RasterizeTriangleMSAA 的具名回调对象，替代函数内部 Lambda。
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

// 作用：执行三角形的 MSAA 光栅化，完成 sample 级覆盖测试、深度测试与像素级 resolve。
// 用法：由 Rasterizer::Rasterize_Triangle 传入缓冲区与 shadePixel 回调；回调接收重心坐标 (w0, w1, w2) 并返回该像素的线性空间颜色。
template <typename ShadePixelFunc>
void RasterizeTriangleMSAA(
    const std::array<glm::vec3, 3>& vertexs,
    const std::array<float, 3>& vertexInvW,
    const std::array<glm::vec3, 3>* worldPositions,
    const std::array<glm::vec3, 3>* worldNormals,
    int width,
    int height,
    std::vector<float>& zBuffer,
    std::vector<float>& sampleZBuffer,
    std::vector<glm::vec3>& sampleColorBuffer,
    std::vector<glm::vec3>& sampleNormalBuffer,
    std::vector<int>& fragmentLut,
    std::vector<Fragment>& fragments,
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
    maxX = std::min(maxX, width - 1);
    maxY = std::min(maxY, height - 1);

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
            const size_t pixelIndex = BufferIndex(i, j, width);
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
                if (depth >= sampleZBuffer[sampleBufferIndex]) {
                    continue;
                }

                sampleZBuffer[sampleBufferIndex] = depth;
                sampleNormalBuffer[sampleBufferIndex] = triangleNormal;
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
                sampleColorBuffer[sampleBufferIndex] = pixelShadedColor;
            }

            const float resolvedDepth = ResolvePixelDepth(pixelIndex, sampleZBuffer, activeSampleCount);
            if (!std::isfinite(resolvedDepth)) {
                continue;
            }

            zBuffer[pixelIndex] = resolvedDepth;

            Fragment frag;
            frag.screenPos = Vec2{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f};
            frag.bufferIndex = pixelIndex;
            frag.depth = resolvedDepth;
            frag.color = ToColor(ResolvePixelColor(pixelIndex, sampleColorBuffer, activeSampleCount));

            // 作用：根据光源阴影贴图判断当前片段是否被遮挡。
            // 用法：先做 NDC 与贴图边界检查，再读取深度，避免越界索引导致阴影随机跳变。
            if (worldPositions != nullptr && Scene::instance) {
                const Scene& scene = *(Scene::instance);
                if (scene.shadowSettings.enableShadowMap && !scene.lights.empty()) {
                    const Light& light = *scene.lights[0];
                    const glm::vec3 worldPos = (*worldPositions)[0] * correctedW0
                        + (*worldPositions)[1] * correctedW1
                        + (*worldPositions)[2] * correctedW2;

                    if (light.type() == Light::LightType::Point) {
                        const glm::vec3 lightToFragment = worldPos - light.position();
                        if (glm::dot(lightToFragment, lightToFragment) > 1e-12f) {
                            const int faceIndex = SelectPointShadowFace(lightToFragment);
                            const int shadowResolution = light.pointShadowResolution();
                            const std::vector<float>& shadowMap = light.pointLightViewDepths(static_cast<std::size_t>(faceIndex));

                            if (shadowResolution > 0
                                && shadowMap.size() == static_cast<std::size_t>(shadowResolution) * static_cast<std::size_t>(shadowResolution)) {
                                glm::vec4 lightSpacePos = light.pointLightSpaceMatrix(static_cast<std::size_t>(faceIndex)) * glm::vec4(worldPos, 1.0f);
                                if (std::abs(lightSpacePos.w) > 1e-6f) {
                                    lightSpacePos /= lightSpacePos.w;

                                    const bool outsideLightFrustum =
                                        (lightSpacePos.x < -1.0f || lightSpacePos.x > 1.0f)
                                        || (lightSpacePos.y < -1.0f || lightSpacePos.y > 1.0f)
                                        || (lightSpacePos.z < -1.0f || lightSpacePos.z > 1.0f);
                                    if (!outsideLightFrustum) {
                                        const int lx = static_cast<int>((lightSpacePos.x + 1.0f) * 0.5f * static_cast<float>(shadowResolution));
                                        const int ly = static_cast<int>((1.0f - (lightSpacePos.y + 1.0f) * 0.5f) * static_cast<float>(shadowResolution));

                                        if (lx >= 0 && lx < shadowResolution && ly >= 0 && ly < shadowResolution) {
                                            const size_t shadowIndex = BufferIndex(lx, ly, shadowResolution);
                                            const float receiverDepth = (lightSpacePos.z + 1.0f) * 0.5f;
                                            const float blockerDepth = shadowMap[shadowIndex];
                                            const float bias = std::max(scene.shadowSettings.depthBias, 0.0f);
                                            frag.shadowVisibility = (receiverDepth - bias > blockerDepth) ? 0.0f : 1.0f;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        const int shadowWidth = light.shadowMapWidth();
                        const int shadowHeight = light.shadowMapHeight();
                        const std::vector<float>& shadowMap = light.lightViewDepths();

                        if (shadowWidth > 0 && shadowHeight > 0
                            && shadowMap.size() == static_cast<std::size_t>(shadowWidth) * static_cast<std::size_t>(shadowHeight)) {
                            glm::vec4 lightSpacePos = light.lightSpaceMatrix() * glm::vec4(worldPos, 1.0f);
                            if (std::abs(lightSpacePos.w) > 1e-6f) {
                                lightSpacePos /= lightSpacePos.w;

                                const bool outsideLightFrustum =
                                    (lightSpacePos.x < -1.0f || lightSpacePos.x > 1.0f)
                                    || (lightSpacePos.y < -1.0f || lightSpacePos.y > 1.0f)
                                    || (lightSpacePos.z < -1.0f || lightSpacePos.z > 1.0f);
                                if (!outsideLightFrustum) {
                                    const int lx = static_cast<int>((lightSpacePos.x + 1.0f) * 0.5f * static_cast<float>(shadowWidth));
                                    const int ly = static_cast<int>((1.0f - (lightSpacePos.y + 1.0f) * 0.5f) * static_cast<float>(shadowHeight));

                                    if (lx >= 0 && lx < shadowWidth && ly >= 0 && ly < shadowHeight) {
                                        const size_t shadowIndex = BufferIndex(lx, ly, shadowWidth);
                                        const float receiverDepth = (lightSpacePos.z + 1.0f) * 0.5f;
                                        const float blockerDepth = shadowMap[shadowIndex];
                                        const float bias = std::max(scene.shadowSettings.depthBias, 0.0f);
                                        frag.shadowVisibility = (receiverDepth - bias > blockerDepth) ? 0.0f : 1.0f;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 作用：将当前像素重心坐标对应的世界空间位置与法线写入 Fragment，供光照在世界坐标系计算。
            // 用法：若调用方提供了 worldPositions/worldNormals，则按重心插值；否则退回到已有法线解析路径。
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
                frag.normal = ResolvePixelNormal(pixelIndex, resolvedDepth, sampleZBuffer, sampleNormalBuffer, activeSampleCount);
            }

            const int fragmentIndex = fragmentLut[pixelIndex];
            if (fragmentIndex >= 0) {
                fragments[static_cast<std::size_t>(fragmentIndex)] = frag;
            } else {
                fragmentLut[pixelIndex] = static_cast<int>(fragments.size());
                fragments.push_back(frag);
            }
        }
    }
}
}

int Rasterizer::width() const
{
    return width_;
}

int Rasterizer::height() const
{
    return height_;
}

void Rasterizer::setMsaaSampleCount(int sampleCount)
{
    msaaSampleCount_ = NormalizeMsaaSampleCount(sampleCount);
}

void Rasterizer::Clear()
{
    // 仅清理当前激活采样档位对应的 sample 缓冲，减少低档 MSAA 下的无效内存写入。
    const int activeSampleCount = msaaSampleCount_;
    const float infDepth = std::numeric_limits<float>::infinity();

    std::fill(zBuffer_.begin(), zBuffer_.end(), std::numeric_limits<float>::infinity());

    for (size_t pixelIndex = 0; pixelIndex < zBuffer_.size(); ++pixelIndex) {
        for (int sampleIndex = 0; sampleIndex < activeSampleCount; ++sampleIndex) {
            const size_t sampleBufferIndex = SampleBufferIndex(pixelIndex, sampleIndex);
            sampleZBuffer_[sampleBufferIndex] = infDepth;
            sampleColorBuffer_[sampleBufferIndex] = glm::vec3(0.0f);
            sampleNormalBuffer_[sampleBufferIndex] = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }

    std::fill(fragmentLut_.begin(), fragmentLut_.end(), -1);
    fragments_.clear();
}

void Rasterizer::Rasterize_Triangle(
    const std::array<Vertex, 3>& vertices,
    const Texture2D* texture,
    const std::array<glm::vec3, 3>* worldPositions,
    const std::array<glm::vec3, 3>* worldNormals)
{
    // 屏幕空间采用 y 轴向下约定时，约定顺时针为正面。
    const Vec2 s0 = {vertices[0].position.x, vertices[0].position.y};
    const Vec2 s1 = {vertices[1].position.x, vertices[1].position.y};
    const Vec2 s2 = {vertices[2].position.x, vertices[2].position.y};
    const float signedArea = VectorMath::edgeFunction(s0, s1, s2);

    if (std::abs(signedArea) <= 1e-6f) {
        return;
    }

    if (backfaceCullingEnabled_) {
        // 说明：当前 edgeFunction 的符号定义与屏幕 Y 轴向下视口变换共同作用后，正面三角形对应 signedArea > 0。
        // 若这里符号写反，会把正面误剔除，导致模型“外观翻面/缺面”。
        const bool isFrontFace = signedArea > 0.0f;
        if (!isFrontFace) {
            return;
        }
    }

    // 如果开启了线框模式，则仅绘制三角形的边缘线段。
    if (wireframeOverlayEnabled_) {
        const glm::vec3 triangleNormal = VectorMath::getNormal(vertices[0].position, vertices[1].position, vertices[2].position);
        RasterizeLine(vertices[0].position, vertices[1].position, triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertices[1].position, vertices[2].position, triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertices[2].position, vertices[0].position, triangleNormal, width_, height_, zBuffer_, fragments_);
        return;
    }

    std::array<glm::vec3, 3> screenPositions = {vertices[0].position, vertices[1].position, vertices[2].position};
    const std::array<float, 3> vertexInvW = {vertices[0].invW, vertices[1].invW, vertices[2].invW};

    // 若传入了有效的纹理对象，则处理 UV 缝隙修复
    std::array<glm::vec2, 3> seamSafeTexCoords = {vertices[0].texCoord, vertices[1].texCoord, vertices[2].texCoord};
    if (texture) {
        const float minU = std::min({vertices[0].texCoord.x, vertices[1].texCoord.x, vertices[2].texCoord.x});
        const float maxU = std::max({vertices[0].texCoord.x, vertices[1].texCoord.x, vertices[2].texCoord.x});
        const bool hasNearZero = (vertices[0].texCoord.x < 0.15f)
            || (vertices[1].texCoord.x < 0.15f)
            || (vertices[2].texCoord.x < 0.15f);
        const bool hasNearOne = (vertices[0].texCoord.x > 0.85f)
            || (vertices[1].texCoord.x > 0.85f)
            || (vertices[2].texCoord.x > 0.85f);
        // 仅在 UV 横跨 [0,1] 边界附近时才做接缝修正，避免误处理普通 UV 岛。
        if ((maxU - minU > 0.5f) && hasNearZero && hasNearOne) {
            for (glm::vec2& uv : seamSafeTexCoords) {
                if (uv.x < 0.5f) {
                    uv.x += 1.0f;
                }
            }
        }
    }

    const int activeSampleCount = msaaSampleCount_;
    const Vec2* sampleOffsets = ResolveMsaaOffsets(activeSampleCount);

    // 调用底层核心 MSAA 渲染函数。
    // 使用具名着色对象，根据重心坐标 (w0, w1, w2) 计算每个被覆盖像素的颜色。
    const ShadePixelWithTexture shadePixel{vertices, seamSafeTexCoords, texture};
    RasterizeTriangleMSAA(
        screenPositions,
        vertexInvW,
        worldPositions,
        worldNormals,
        width_,
        height_,
        zBuffer_,
        sampleZBuffer_,
        sampleColorBuffer_,
        sampleNormalBuffer_,
        fragmentLut_,
        fragments_,
        activeSampleCount,
        sampleOffsets,
        shadePixel);
}
