#pragma once
#include "include/Rasterizer.h"

void Rasterizer::Rasterize_Triangle(std::vector<glm::vec3>& vertexs, std::vector<glm::vec3>& colors)
{
    // 这里应该实现三角形光栅化算法，使用 vertexs 中的顶点坐标和 colors 中的颜色进行插值。
    // 你可以使用边函数法或者扫描线法来实现光栅化。
    // 注意要进行深度测试，更新 zBuffer_ 中的深度值，并且在通过测试后更新 colorBuffer_ 中的颜色值。

    int minX = static_cast<int>(std::floor(std::min(vertexs[0].x, vertexs[1].x, vertexs[2].x)));
    int maxX = static_cast<int>(std::ceil(std::max(vertexs[0].x, vertexs[1].x, vertexs[2].x)));
    int minY = static_cast<int>(std::floor(std::min(vertexs[0].y, vertexs[1].y, vertexs[2].y)));
    int maxY = static_cast<int>(std::ceil(std::max(vertexs[0].y, vertexs[1].y, vertexs[2].y)));

    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, width_ - 1);
    maxY = std::min(maxY, height_ - 1);

    Vec2 v0 = {vertexs[0].x, vertexs[0].y};
    Vec2 v1 = {vertexs[1].x, vertexs[1].y};
    Vec2 v2 = {vertexs[2].x, vertexs[2].y};

    for(int i=minX;i<=maxX;i++)
    {
        for(int j=minY;j<=maxY;j++)
        {
            // 判断点 (i, j) 是否在三角形内，如果在三角形内，计算该点的颜色和深度，并更新 colorBuffer_ 和 zBuffer_。
            Vec2 p{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f}; // 采样点在像素中心
            float w0 = VectorMath::edgeFunction(v1, v2, p);
            float w1 = VectorMath::edgeFunction(v2, v0, p);
            float w2 = VectorMath::edgeFunction(v0, v1, p);
            float area = VectorMath::edgeFunction(v0, v1, v2);

            if (area <= 1e-6f) {
                continue; // 三角形退化，跳过
            }

            const bool isAreaPositive = area > 0.0f;
            const bool inside = isAreaPositive
                ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if(inside){
                area = 1.0f / area;
                w0 *= area;
                w1 *= area;
                w2 *= area;
                float depth = VectorMath::InterpDepth(std::vector<Vec2>{v0, v1, v2}, std::vector<float>{w0, w1, w2});
                size_t bufferIndex = BufferIndex(i, j, width_);
                if (depth < zBuffer_[bufferIndex]) { // 深度测试
                    zBuffer_[bufferIndex] = depth;
                    Fragment frag;
                    frag.screenPos = p;
                    frag.depth = depth;
                    frag.color = VectorMath::InterpColor(colors, std::vector<float>{w0, w1, w2});
                    fragments_[bufferIndex] = frag;
                }
            }
        }
    }
}