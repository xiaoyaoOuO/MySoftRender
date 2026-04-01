#include "Rasterizer.h"
#include "Texture.h"

namespace {
void RasterizeLine(
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& normal,
    int width,
    int height,
    std::vector<float>& zBuffer,
    std::vector<Fragment>& fragments)
{
    constexpr float kEdgeDepthBias = 1e-4f;
    constexpr Color kEdgeColor = {255, 255, 255, 255};

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
    fragments_.clear();
}

void Rasterizer::Rasterize_Triangle(const std::array<glm::vec3, 3>& vertexs, const std::array<glm::vec3, 3>& colors)
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

    Vec2 v0 = {vertexs[0].x, vertexs[0].y};
    Vec2 v1 = {vertexs[1].x, vertexs[1].y};
    Vec2 v2 = {vertexs[2].x, vertexs[2].y};

    float area = VectorMath::edgeFunction(v0, v1, v2);
    if (std::abs(area) <= 1e-6f) {
        return; // 三角形退化，跳过
    }

    const glm::vec3 triangleNormal = VectorMath::getNormal(vertexs[0], vertexs[1], vertexs[2]);

    const bool isAreaPositive = area > 0.0f;
    const float invArea = 1.0f / area;

    const float z0 = vertexs[0].z;
    const float z1 = vertexs[1].z;
    const float z2 = vertexs[2].z;

    
    if (wireframeOverlayEnabled_) {
        RasterizeLine(vertexs[0], vertexs[1], triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertexs[1], vertexs[2], triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertexs[2], vertexs[0], triangleNormal, width_, height_, zBuffer_, fragments_);
        return; // 线框模式下只绘制边线
    }

    for(int j=minY;j<=maxY;j++)
    {
        for(int i=minX;i<=maxX;i++)
        {
            // 判断点 (i, j) 是否在三角形内，如果在三角形内，计算该点的颜色和深度，并更新 colorBuffer_ 和 zBuffer_。
            Vec2 p{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f}; // 采样点在像素中心
            float w0 = VectorMath::edgeFunction(v1, v2, p);
            float w1 = VectorMath::edgeFunction(v2, v0, p);
            float w2 = VectorMath::edgeFunction(v0, v1, p);
            const bool inside = isAreaPositive
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if(inside){
                w0 *= invArea;
                w1 *= invArea;
                w2 *= invArea;
                float depth = z0 * w0 + z1 * w1 + z2 * w2;
                size_t bufferIndex = BufferIndex(i, j, width_);
                if (depth < zBuffer_[bufferIndex]) { // 深度测试
                    zBuffer_[bufferIndex] = depth;
                    Fragment frag;
                    frag.screenPos = p;
                    frag.depth = depth;
                    frag.normal = triangleNormal;
                    const glm::vec3 color = colors[0] * w0 + colors[1] * w1 + colors[2] * w2;
                    frag.color = Color{
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.r * 255.0f), 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.g * 255.0f), 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(color.b * 255.0f), 0, 255)),
                        255
                    };
                    frag.bufferIndex = bufferIndex;
                    fragments_.push_back(frag);
                }
            }
        }
    }
}

void Rasterizer::Rasterize_Triangle(
    const std::array<glm::vec3, 3>& vertexs,
    const std::array<glm::vec3, 3>& colors,
    const std::array<glm::vec2, 3>& texCoords,
    const Texture2D& texture)
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

    Vec2 v0 = {vertexs[0].x, vertexs[0].y};
    Vec2 v1 = {vertexs[1].x, vertexs[1].y};
    Vec2 v2 = {vertexs[2].x, vertexs[2].y};

    float area = VectorMath::edgeFunction(v0, v1, v2);
    if (std::abs(area) <= 1e-6f) {
        return;
    }

    const glm::vec3 triangleNormal = VectorMath::getNormal(vertexs[0], vertexs[1], vertexs[2]);

    const bool isAreaPositive = area > 0.0f;
    const float invArea = 1.0f / area;

    const float z0 = vertexs[0].z;
    const float z1 = vertexs[1].z;
    const float z2 = vertexs[2].z;

    if (wireframeOverlayEnabled_) {
        RasterizeLine(vertexs[0], vertexs[1], triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertexs[1], vertexs[2], triangleNormal, width_, height_, zBuffer_, fragments_);
        RasterizeLine(vertexs[2], vertexs[0], triangleNormal, width_, height_, zBuffer_, fragments_);
        return;
    }

    std::array<glm::vec2, 3> seamSafeTexCoords = texCoords;
    const float minU = std::min({texCoords[0].x, texCoords[1].x, texCoords[2].x});
    const float maxU = std::max({texCoords[0].x, texCoords[1].x, texCoords[2].x});
    if (maxU - minU > 0.5f) {
        for (glm::vec2& uv : seamSafeTexCoords) {
            if (uv.x < 0.5f) {
                uv.x += 1.0f;
            }
        }
    }

    for (int j = minY; j <= maxY; ++j) {
        for (int i = minX; i <= maxX; ++i) {
            Vec2 p{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f};
            float w0 = VectorMath::edgeFunction(v1, v2, p);
            float w1 = VectorMath::edgeFunction(v2, v0, p);
            float w2 = VectorMath::edgeFunction(v0, v1, p);
            const bool inside = isAreaPositive
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (inside) {
                w0 *= invArea;
                w1 *= invArea;
                w2 *= invArea;

                const float depth = z0 * w0 + z1 * w1 + z2 * w2;
                const size_t bufferIndex = BufferIndex(i, j, width_);
                if (depth < zBuffer_[bufferIndex]) {
                    zBuffer_[bufferIndex] = depth;

                    float u = seamSafeTexCoords[0].x * w0 + seamSafeTexCoords[1].x * w1 + seamSafeTexCoords[2].x * w2;
                    u -= std::floor(u);
                    const float v = glm::clamp(
                        seamSafeTexCoords[0].y * w0 + seamSafeTexCoords[1].y * w1 + seamSafeTexCoords[2].y * w2,
                        0.0f,
                        1.0f);

                    const glm::vec3 vertexColor = glm::clamp(
                        colors[0] * w0 + colors[1] * w1 + colors[2] * w2,
                        glm::vec3(0.0f),
                        glm::vec3(1.0f));
                    const glm::vec3 texturedColor = texture.sample(u, v) * vertexColor;

                    Fragment frag;
                    frag.screenPos = p;
                    frag.depth = depth;
                    frag.normal = triangleNormal;
                    frag.color = Color{
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(texturedColor.r * 255.0f), 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(texturedColor.g * 255.0f), 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(static_cast<int>(texturedColor.b * 255.0f), 0, 255)),
                        255
                    };
                    frag.bufferIndex = bufferIndex;
                    fragments_.push_back(frag);
                }
            }
        }
    }
}