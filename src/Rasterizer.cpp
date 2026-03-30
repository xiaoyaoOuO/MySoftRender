#include "Rasterizer.h"

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
    // 这里应该实现三角形光栅化算法，使用 vertexs 中的顶点坐标和 colors 中的颜色进行插值。
    // 你可以使用边函数法或者扫描线法来实现光栅化。
    // 注意要进行深度测试，更新 zBuffer_ 中的深度值，并且在通过测试后更新 colorBuffer_ 中的颜色值。

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

    const bool isAreaPositive = area > 0.0f;
    const float invArea = 1.0f / area;

    const float z0 = vertexs[0].z;
    const float z1 = vertexs[1].z;
    const float z2 = vertexs[2].z;

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
                    frag.normal = VectorMath::getNormal(vertexs[0], vertexs[1], vertexs[2]);
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