#include "Rasterizer.h"
#include "Texture.h"

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

void RasterizeLine(
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

    const int x0 = static_cast<int>(std::round(v0.x));
    const int y0 = static_cast<int>(std::round(v0.y));
    const int x1 = static_cast<int>(std::round(v1.x));
    const int y1 = static_cast<int>(std::round(v1.y));

    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int steps = std::max(dx, dy);

    auto tryEmitFragment = [&](int x, int y, float t)
    {
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
    };

    if (steps == 0) {
        tryEmitFragment(x0, y0, 0.0f);
        return;
    }

    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const float xf = static_cast<float>(x0) + static_cast<float>(x1 - x0) * t;
        const float yf = static_cast<float>(y0) + static_cast<float>(y1 - y0) * t;
        const int x = static_cast<int>(std::round(xf));
        const int y = static_cast<int>(std::round(yf));
        tryEmitFragment(x, y, t);
    }
}

// 作用：执行三角形的 MSAA 光栅化，完成 sample 级覆盖测试、深度测试与像素级 resolve。
// 用法：由 Rasterizer::Rasterize_Triangle 传入缓冲区与 shadePixel 回调；回调接收重心坐标 (w0, w1, w2) 并返回该像素的线性空间颜色。
template <typename ShadePixelFunc>
void RasterizeTriangleMSAA(
    const std::array<glm::vec3, 3>& vertexs,
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
            const glm::vec3 pixelShadedColor = glm::clamp(
                shadePixel(shadeW0, shadeW1, shadeW2),
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

            // 作用：将当前像素重心坐标对应的世界空间位置与法线写入 Fragment，供光照在世界坐标系计算。
            // 用法：若调用方提供了 worldPositions/worldNormals，则按重心插值；否则退回到已有法线解析路径。
            if (worldPositions != nullptr) {
                frag.worldPos = (*worldPositions)[0] * shadeW0
                    + (*worldPositions)[1] * shadeW1
                    + (*worldPositions)[2] * shadeW2;
            } else {
                frag.worldPos = glm::vec3(0.0f);
            }

            if (worldNormals != nullptr) {
                glm::vec3 interpolatedNormal = (*worldNormals)[0] * shadeW0
                    + (*worldNormals)[1] * shadeW1
                    + (*worldNormals)[2] * shadeW2;
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
    // 在 Lambda 回调中，通过计算得到的重心坐标(w0, w1, w2)插值计算每个被覆盖像素的颜色。
    RasterizeTriangleMSAA(
        screenPositions,
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
        [&](float w0, float w1, float w2) {
            // 基本颜色插值
            const glm::vec3 vertexColor = vertices[0].color * w0 + vertices[1].color * w1 + vertices[2].color * w2;
            
            // 无纹理时直接返回顶点颜色插值
            if (!texture) {
                return vertexColor;
            }

            // 处理有纹理情况，对纹理坐标进行插值
            float u = seamSafeTexCoords[0].x * w0 + seamSafeTexCoords[1].x * w1 + seamSafeTexCoords[2].x * w2;
            u -= std::floor(u);
            const float v = std::clamp(
                seamSafeTexCoords[0].y * w0 + seamSafeTexCoords[1].y * w1 + seamSafeTexCoords[2].y * w2,
                0.0f,
                1.0f);

            return texture->sample(u, v) * vertexColor;
        });
}
