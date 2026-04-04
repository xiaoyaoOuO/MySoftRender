#include "Rasterizer.h"
#include "Texture.h"

namespace {
// 作用：定义当前光栅器使用的固定 4x MSAA 采样数量。
// 用法：所有 sample 缓冲都按 pixelIndex * kMsaaSamples 展开存储，若调整采样数需同步修改偏移与缓冲初始化逻辑。
constexpr int kMsaaSamples = 4;
// 作用：定义每个像素内部 4 个子采样点的偏移位置。
// 用法：在遍历像素时，将像素左上角整数坐标加上该偏移得到 samplePoint，用于覆盖测试与重心插值。
constexpr std::array<Vec2, kMsaaSamples> kMsaaOffsets = {
    Vec2{0.25f, 0.25f},
    Vec2{0.75f, 0.25f},
    Vec2{0.25f, 0.75f},
    Vec2{0.75f, 0.75f}
};

// 作用：把像素索引与子采样索引映射到 sample 缓冲的一维下标。
// 用法：访问 sampleZBuffer、sampleColorBuffer、sampleNormalBuffer 时统一调用，sampleIndex 范围为 [0, kMsaaSamples)。
size_t SampleBufferIndex(size_t pixelIndex, int sampleIndex)
{
    return pixelIndex * static_cast<size_t>(kMsaaSamples) + static_cast<size_t>(sampleIndex);
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
float ResolvePixelDepth(size_t pixelIndex, const std::vector<float>& sampleZBuffer)
{
    float minDepth = std::numeric_limits<float>::infinity();
    for (int sampleIndex = 0; sampleIndex < kMsaaSamples; ++sampleIndex) {
        minDepth = std::min(minDepth, sampleZBuffer[SampleBufferIndex(pixelIndex, sampleIndex)]);
    }
    return minDepth;
}

// 作用：将一个像素的 4 个 sample 颜色做平均，得到最终像素颜色。
// 用法：在通过深度测试的 sample 写入颜色后调用，结果再通过 ToColor 转为 8-bit RGBA 输出。
glm::vec3 ResolvePixelColor(size_t pixelIndex, const std::vector<glm::vec3>& sampleColorBuffer)
{
    glm::vec3 accumulated(0.0f);
    for (int sampleIndex = 0; sampleIndex < kMsaaSamples; ++sampleIndex) {
        accumulated += sampleColorBuffer[SampleBufferIndex(pixelIndex, sampleIndex)];
    }
    return accumulated * (1.0f / static_cast<float>(kMsaaSamples));
}

// 作用：从与解析深度匹配的 sample 中恢复像素法线，供后续调试显示或可视化使用。
// 用法：先传入 ResolvePixelDepth 的结果，再从 sampleNormalBuffer 中挑选深度最接近的法线返回。
glm::vec3 ResolvePixelNormal(
    size_t pixelIndex,
    float resolvedDepth,
    const std::vector<float>& sampleZBuffer,
    const std::vector<glm::vec3>& sampleNormalBuffer)
{
    static constexpr float kDepthEpsilon = 1e-6f;
    for (int sampleIndex = 0; sampleIndex < kMsaaSamples; ++sampleIndex) {
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
    int width,
    int height,
    std::vector<float>& zBuffer,
    std::vector<float>& sampleZBuffer,
    std::vector<glm::vec3>& sampleColorBuffer,
    std::vector<glm::vec3>& sampleNormalBuffer,
    std::vector<int>& fragmentLut,
    std::vector<Fragment>& fragments,
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
    const glm::vec3 triangleNormal = VectorMath::getNormal(vertexs[0], vertexs[1], vertexs[2]);

    const float z0 = vertexs[0].z;
    const float z1 = vertexs[1].z;
    const float z2 = vertexs[2].z;

    for (int j = minY; j <= maxY; ++j) {
        for (int i = minX; i <= maxX; ++i) {
            const size_t pixelIndex = BufferIndex(i, j, width);
            std::array<bool, kMsaaSamples> samplePassed = {false, false, false, false};
            int passedSampleCount = 0;
            float centroidX = 0.0f;
            float centroidY = 0.0f;

            for (int sampleIndex = 0; sampleIndex < kMsaaSamples; ++sampleIndex) {
                const Vec2 samplePoint = {
                    static_cast<float>(i) + kMsaaOffsets[sampleIndex].x,
                    static_cast<float>(j) + kMsaaOffsets[sampleIndex].y
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
            if (passedSampleCount != kMsaaSamples) {
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

            for (int sampleIndex = 0; sampleIndex < kMsaaSamples; ++sampleIndex) {
                if (!samplePassed[sampleIndex]) {
                    continue;
                }
                const size_t sampleBufferIndex = SampleBufferIndex(pixelIndex, sampleIndex);
                sampleColorBuffer[sampleBufferIndex] = pixelShadedColor;
            }

            const float resolvedDepth = ResolvePixelDepth(pixelIndex, sampleZBuffer);
            if (!std::isfinite(resolvedDepth)) {
                continue;
            }

            zBuffer[pixelIndex] = resolvedDepth;

            Fragment frag;
            frag.screenPos = Vec2{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f};
            frag.bufferIndex = pixelIndex;
            frag.depth = resolvedDepth;
            frag.color = ToColor(ResolvePixelColor(pixelIndex, sampleColorBuffer));
            frag.normal = ResolvePixelNormal(pixelIndex, resolvedDepth, sampleZBuffer, sampleNormalBuffer);

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

void Rasterizer::Clear()
{
    std::fill(zBuffer_.begin(), zBuffer_.end(), std::numeric_limits<float>::infinity());
    std::fill(sampleZBuffer_.begin(), sampleZBuffer_.end(), std::numeric_limits<float>::infinity());
    std::fill(sampleColorBuffer_.begin(), sampleColorBuffer_.end(), glm::vec3(0.0f));
    std::fill(sampleNormalBuffer_.begin(), sampleNormalBuffer_.end(), glm::vec3(0.0f, 0.0f, 1.0f));
    std::fill(fragmentLut_.begin(), fragmentLut_.end(), -1);
    fragments_.clear();
}

void Rasterizer::Rasterize_Triangle(const std::array<Vertex, 3>& vertices, const Texture2D* texture)
{
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
        if (maxU - minU > 0.5f) {
            for (glm::vec2& uv : seamSafeTexCoords) {
                if (uv.x < 0.5f) {
                    uv.x += 1.0f;
                }
            }
        }
    }

    // 调用底层核心 MSAA 渲染函数。
    // 在 Lambda 回调中，通过计算得到的重心坐标(w0, w1, w2)插值计算每个被覆盖像素的颜色。
    RasterizeTriangleMSAA(
        screenPositions,
        width_,
        height_,
        zBuffer_,
        sampleZBuffer_,
        sampleColorBuffer_,
        sampleNormalBuffer_,
        fragmentLut_,
        fragments_,
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
