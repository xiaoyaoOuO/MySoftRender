#pragma once

#include <cstdint>
#include <string>
#include <vector>

class CubemapTexture;

namespace utility {

/**
 * @brief LUT 图像数据容器，使用 RGB8 连续字节存储。
 */
struct SkyboxLutImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb8;

    /**
     * @brief 判断 LUT 数据是否完整可用。
     * @return 数据完整返回 true，否则返回 false。
     */
    bool valid() const;
};

/**
 * @brief 生成 BRDF 积分 LUT 的配置参数。
 */
struct SkyboxLutBuildConfig
{
    int width = 128;
    int height = 128;
    int sampleCount = 64;
    // 保留旧字段用于兼容命令行参数，BRDF LUT 生成流程中不再使用。
    float blurRadiusMin = 0.01f;
    float blurRadiusMax = 0.45f;
};

/**
 * @brief 天空盒 LUT 生成工具，负责离线生成与导出。
 */
class SkyboxLutGenerator
{
public:
    /**
    * @brief 生成传统 Split-Sum BRDF LUT（R=scale, G=bias）。
    * @param skybox 兼容旧接口保留，当前 BRDF LUT 生成不依赖天空盒颜色。
    * @param config 生成参数（分辨率、积分采样数）。
    * @return 返回 RGB8 LUT 图像。
     */
    static SkyboxLutImage GenerateFromSkybox(
        const CubemapTexture& skybox,
        const SkyboxLutBuildConfig& config = SkyboxLutBuildConfig());

    /**
     * @brief 将 LUT 图像导出为 PNG 文件。
     * @param image 待保存 LUT 图像。
     * @param filePath 输出文件路径。
     * @return 保存成功返回 true，否则返回 false。
     */
    static bool SaveAsPNG(const SkyboxLutImage& image, const std::string& filePath);

    /**
     * @brief 将 LUT 图像导出为二进制 PPM（P6）文件。
     * @param image 待保存 LUT 图像。
     * @param filePath 输出文件路径。
     * @return 保存成功返回 true，否则返回 false。
     */
    static bool SaveAsPPM(const SkyboxLutImage& image, const std::string& filePath);
};

} // namespace utility
