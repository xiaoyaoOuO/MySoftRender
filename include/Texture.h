#pragma once

#include <array>
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
 * @brief 立方体贴图资源，负责六面纹理加载与方向采样。
 */
class CubemapTexture
{
public:
    /**
     * @brief 从目录中解析并加载六个面纹理。
     * @param directoryPath 天空盒目录路径，支持 posx/negx/posy/negy/posz/negz 命名。
     */
    bool loadFromDirectory(const std::string& directoryPath);

    /**
     * @brief 按方向向量采样天空盒颜色。
     * @param direction 世界空间方向向量，建议传入单位向量。
     * @return 返回线性空间 RGB 颜色；若资源无效返回洋红色。
     */
    glm::vec3 sample(const glm::vec3& direction) const;

    // 查询天空盒资源是否可采样。六个面全部加载成功且尺寸一致时返回 true。
    bool valid() const { return valid_; }

    // 获取天空盒来源目录路径。用于日志输出或调试面板显示当前资源来源。
    const std::string& directoryPath() const { return directoryPath_; }

private:
    enum class Face : std::size_t
    {
        PositiveX = 0,
        NegativeX,
        PositiveY,
        NegativeY,
        PositiveZ,
        NegativeZ
    };

    // 根据方向向量选择立方体面，并输出该面的局部 uv（范围 [-1,1]）。
    static Face selectFace(const glm::vec3& direction, float& outU, float& outV);

    std::array<Texture2D, 6> faces_;
    bool valid_ = false;
    int faceWidth_ = 0;
    int faceHeight_ = 0;
    std::string directoryPath_;
};
