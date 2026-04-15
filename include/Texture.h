#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

class Texture2D
{
public:
    // 从磁盘文件加载纹理并转换为 RGB8 像素缓存。传入图片路径，必要时打开 flipVertically 以匹配 UV 方向；返回是否加载成功。
    bool loadFromFile(const std::string& filePath, bool flipVertically = false);

    // 程序化生成棋盘格纹理，常用于资源缺失时回退显示。设置分辨率、格子尺寸与两种颜色，函数会覆盖当前纹理数据。
    void createCheckerboard(
        int width,
        int height,
        int tileSize,
        const glm::vec3& colorA = glm::vec3(0.95f, 0.95f, 0.95f),
        const glm::vec3& colorB = glm::vec3(0.15f, 0.35f, 0.75f));

    // 按 UV 坐标执行双线性采样，返回线性空间 RGB 颜色。传入 [0,1] 范围附近的 u/v，u 会环绕，v 会钳制。
    glm::vec3 sample(float u, float v) const;

    // 查询纹理是否处于可采样状态。在渲染或采样前先判断，避免空纹理访问。
    bool valid() const { return width_ > 0 && height_ > 0 && !pixels_.empty(); }

    // 获取纹理宽度（像素）。用于调试、UI 显示或采样步长计算。
    int width() const { return width_; }

    // 获取纹理高度（像素）。用于调试、UI 显示或采样步长计算。
    int height() const { return height_; }

private:
    // 按整数像素坐标读取一个 texel，并自动处理越界钳制。作为 sample 的底层读取接口，一般不直接在外部调用。
    glm::vec3 texel(int x, int y) const;

    // 将 [0,1] 浮点颜色通道转换为 8-bit 字节值。用于生成/写入 RGB8 纹理数据时的统一量化。
    static std::uint8_t toByte(float value);

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<std::uint8_t> pixels_;
};

/**
 * @brief 立方体纹理，按 +X/-X/+Y/-Y/+Z/-Z 六个面存储环境贴图。
 */
class TextureCube
{
public:
    static constexpr std::size_t kFaceCount = 6;

    /**
     * @brief 立方体贴图面索引定义，顺序固定为 +X/-X/+Y/-Y/+Z/-Z。
     */
    enum class Face : std::size_t
    {
        PositiveX = 0,
        NegativeX = 1,
        PositiveY = 2,
        NegativeY = 3,
        PositiveZ = 4,
        NegativeZ = 5
    };

    // 从六张图片加载立方体贴图。传入固定顺序的文件路径数组，全部成功时返回 true。
    bool loadFromFiles(const std::array<std::string, kFaceCount>& filePaths, bool flipVertically = false);

    // 创建可视化调试立方体贴图。资源缺失时调用，可快速确认天空盒方向是否正确。
    void createDebugFaces(int faceSize = 256);

    // 按方向向量采样环境颜色。输入为世界空间方向（可非单位向量），内部会自动归一化。
    glm::vec3 sample(const glm::vec3& direction) const;

    // 查询立方体贴图是否可用。仅当六个面都成功初始化后返回 true。
    bool valid() const { return valid_; }

private:
    std::array<Texture2D, kFaceCount> faces_;
    bool valid_ = false;
};
