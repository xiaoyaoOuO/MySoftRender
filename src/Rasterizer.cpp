#include "Rasterizer.h"


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
    const std::array<glm::vec3, 3>* worldNormals,
    std::weak_ptr<Material> material)
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
        activeSampleCount,
        sampleOffsets,
        shadePixel,
        // 关键修复：把对象材质透传到片元，确保 roughness/metallic 在着色阶段生效。
        material);
}
