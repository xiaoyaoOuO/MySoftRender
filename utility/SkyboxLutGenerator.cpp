#include "SkyboxLutGenerator.h"

#include "Texture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <stb_image_write.h>

namespace utility {
namespace {

// 将浮点值钳制到 [0,1]，用于颜色与参数安全约束。
float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

// 将 [0,1] 浮点通道量化为 8-bit，供 LUT 输出写入。
std::uint8_t ToByte(float value)
{
    const float clamped = Clamp01(value);
    return static_cast<std::uint8_t>(std::round(clamped * 255.0f));
}

// Van der Corput 序列（base-2）反转位实现，用于构建低差异采样序列。
float RadicalInverseVdC(std::uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

// Hammersley 2D 采样点，x 用均匀分布，y 用 VdC 低差异分布。
glm::vec2 Hammersley(std::uint32_t sampleIndex, std::uint32_t sampleCount)
{
    const float x = (sampleCount > 0u)
        ? static_cast<float>(sampleIndex) / static_cast<float>(sampleCount)
        : 0.0f;
    const float y = RadicalInverseVdC(sampleIndex);
    return glm::vec2(x, y);
}

// 针对法线方向构建正交基，便于把半角向量从切线空间映射到世界空间。
void BuildDirectionBasis(const glm::vec3& centerDir, glm::vec3& tangent, glm::vec3& bitangent)
{
    const glm::vec3 helperUp = (std::abs(centerDir.y) < 0.999f)
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);

    tangent = glm::normalize(glm::cross(helperUp, centerDir));
    bitangent = glm::normalize(glm::cross(centerDir, tangent));
}

// Schlick-GGX 几何项的单边近似。
float GeometrySchlickGGX(float ndotv, float roughness)
{
    const float safeNdotV = std::clamp(ndotv, 0.0f, 1.0f);
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);

    // BRDF LUT 积分属于 IBL 路径，这里使用 IBL 常用的 k = (roughness^2) / 2。
    const float k = (safeRoughness * safeRoughness) * 0.5f;
    return safeNdotV / std::max(safeNdotV * (1.0f - k) + k, 1e-6f);
}

// Smith 联合几何项。
float GeometrySmith(float ndotv, float ndotl, float roughness)
{
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

// GGX 重要性采样半角向量 H。
glm::vec3 ImportanceSampleGGX(const glm::vec2& xi, float roughness, const glm::vec3& normal)
{
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);
    const float alpha = std::max(safeRoughness * safeRoughness, 1e-4f);
    const float alpha2 = alpha * alpha;

    const float phi = 2.0f * glm::pi<float>() * xi.x;
    const float denom = std::max(1.0f + (alpha2 - 1.0f) * xi.y, 1e-6f);
    const float cosTheta = std::sqrt((1.0f - xi.y) / denom);
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));

    const glm::vec3 hTangent(
        std::cos(phi) * sinTheta,
        std::sin(phi) * sinTheta,
        cosTheta);

    glm::vec3 tangent(1.0f, 0.0f, 0.0f);
    glm::vec3 bitangent(0.0f, 1.0f, 0.0f);
    BuildDirectionBasis(normal, tangent, bitangent);

    glm::vec3 halfVector = tangent * hTangent.x + bitangent * hTangent.y + normal * hTangent.z;
    if (glm::dot(halfVector, halfVector) <= 1e-12f) {
        return normal;
    }
    return glm::normalize(halfVector);
}

// 对单个 (NdotV, roughness) 积分 Split-Sum BRDF，返回 (scale, bias)。
glm::vec2 IntegrateBrdf(float ndotv, float roughness, int sampleCount)
{
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const float safeNdotV = std::clamp(ndotv, 0.001f, 0.999f);
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);
    const int safeSampleCount = std::max(sampleCount, 1);

    const float sinTheta = std::sqrt(std::max(1.0f - safeNdotV * safeNdotV, 0.0f));
    const glm::vec3 viewDir(sinTheta, 0.0f, safeNdotV);

    float accumScale = 0.0f;
    float accumBias = 0.0f;

    for (int sampleIndex = 0; sampleIndex < safeSampleCount; ++sampleIndex) {
        const glm::vec2 xi = Hammersley(
            static_cast<std::uint32_t>(sampleIndex),
            static_cast<std::uint32_t>(safeSampleCount));

        const glm::vec3 halfVector = ImportanceSampleGGX(xi, safeRoughness, normal);

        glm::vec3 lightDir = 2.0f * glm::dot(viewDir, halfVector) * halfVector - viewDir;
        if (glm::dot(lightDir, lightDir) <= 1e-12f) {
            continue;
        }
        lightDir = glm::normalize(lightDir);

        const float ndotl = std::max(lightDir.z, 0.0f);
        const float ndoth = std::max(halfVector.z, 0.0f);
        const float vdoth = std::max(glm::dot(viewDir, halfVector), 0.0f);
        if (ndotl <= 0.0f) {
            continue;
        }

        const float geometry = GeometrySmith(safeNdotV, ndotl, safeRoughness);
        const float gVis = (geometry * vdoth) / std::max(ndoth * safeNdotV, 1e-6f);
        const float fresnel = std::pow(1.0f - vdoth, 5.0f);

        accumScale += (1.0f - fresnel) * gVis;
        accumBias += fresnel * gVis;
    }

    const float invSampleCount = 1.0f / static_cast<float>(safeSampleCount);
    return glm::vec2(accumScale * invSampleCount, accumBias * invSampleCount);
}

// 把偏移后的方向写入 LUT 对应像素。
void WriteLutPixel(SkyboxLutImage& lutImage, int x, int y, const glm::vec3& color)
{
    const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(lutImage.width)
        + static_cast<std::size_t>(x);
    const std::size_t byteIndex = pixelIndex * 3u;

    lutImage.rgb8[byteIndex + 0u] = ToByte(color.r);
    lutImage.rgb8[byteIndex + 1u] = ToByte(color.g);
    lutImage.rgb8[byteIndex + 2u] = ToByte(color.b);
}

} // namespace

bool SkyboxLutImage::valid() const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const std::size_t expectedSize = static_cast<std::size_t>(width)
        * static_cast<std::size_t>(height)
        * 3u;
    return rgb8.size() == expectedSize;
}

SkyboxLutImage SkyboxLutGenerator::GenerateFromSkybox(
    const CubemapTexture& skybox,
    const SkyboxLutBuildConfig& config)
{
    (void)skybox;

    SkyboxLutImage lutImage;

    const int lutWidth = std::max(config.width, 2);
    const int lutHeight = std::max(config.height, 2);
    const int sampleCount = std::max(config.sampleCount, 1);

    lutImage.width = lutWidth;
    lutImage.height = lutHeight;
    lutImage.rgb8.resize(
        static_cast<std::size_t>(lutWidth) * static_cast<std::size_t>(lutHeight) * 3u,
        0u);

    for (int y = 0; y < lutHeight; ++y) {
        // 传统 BRDF LUT 纵轴定义为 NdotV。
        const float ndotv = glm::clamp(
            (static_cast<float>(y) + 0.5f) / static_cast<float>(lutHeight),
            0.001f,
            0.999f);

        for (int x = 0; x < lutWidth; ++x) {
            // 传统 BRDF LUT 横轴定义为 roughness。
            const float roughness = glm::clamp(
                (static_cast<float>(x) + 0.5f) / static_cast<float>(lutWidth),
                0.0f,
                1.0f);

            const glm::vec2 brdfSample = IntegrateBrdf(ndotv, roughness, sampleCount);
            const glm::vec3 finalColor(brdfSample.x, brdfSample.y, 0.0f);
            WriteLutPixel(lutImage, x, y, finalColor);
        }
    }

    return lutImage;
}

bool SkyboxLutGenerator::SaveAsPNG(const SkyboxLutImage& image, const std::string& filePath)
{
    if (!image.valid()) {
        return false;
    }

    const int writeOk = stbi_write_png(
        filePath.c_str(),
        image.width,
        image.height,
        3,
        image.rgb8.data(),
        image.width * 3);

    return writeOk != 0;
}

bool SkyboxLutGenerator::SaveAsPPM(const SkyboxLutImage& image, const std::string& filePath)
{
    if (!image.valid()) {
        return false;
    }

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile.is_open()) {
        return false;
    }

    // 使用最简单且依赖最少的 PPM(P6) 导出，方便离线验证 LUT 结果。
    outFile << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    outFile.write(
        reinterpret_cast<const char*>(image.rgb8.data()),
        static_cast<std::streamsize>(image.rgb8.size()));

    return outFile.good();
}

} // namespace utility
