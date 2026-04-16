#include "Texture.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <unordered_map>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <stb_image.h>

namespace {
// 提取浮点数的小数部分，用于 U 坐标环绕采样。传入任意实数，返回 [0,1) 范围的小数部分。
float Fract(float value)
{
    return value - std::floor(value);
}

// 将字符串转换为小写 ASCII，便于做大小写无关的文件名匹配。
std::string ToLowerAscii(const std::string& text)
{
    std::string lower = text;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return lower;
}

// 扫描目录下的普通文件，并建立 stem -> 完整路径映射，供天空盒面别名查找使用。
std::unordered_map<std::string, std::filesystem::path> BuildStemPathMap(const std::filesystem::path& directoryPath)
{
    namespace fs = std::filesystem;
    std::unordered_map<std::string, fs::path> stemPathMap;

    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(directoryPath, ec)) {
        if (ec) {
            stemPathMap.clear();
            return stemPathMap;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const std::string stem = ToLowerAscii(entry.path().stem().string());
        if (!stem.empty()) {
            stemPathMap[stem] = entry.path();
        }
    }
    return stemPathMap;
}
}

// 把 [0,1] 浮点通道值量化为 8-bit 通道值。内部用于纹理生成写入，超范围输入会先做钳制。
std::uint8_t Texture2D::toByte(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::round(clamped * 255.0f));
}

// 使用 stb_image 从文件加载图片，并转换为统一的 RGB 三通道格式。传入文件路径与是否垂直翻转标志，成功返回 true，失败会清空纹理状态。
bool Texture2D::loadFromFile(const std::string& filePath, bool flipVertically)
{
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

    int loadedWidth = 0;
    int loadedHeight = 0;
    int loadedChannels = 0;

    unsigned char* data = stbi_load(filePath.c_str(), &loadedWidth, &loadedHeight, &loadedChannels, 3);
    if (data == nullptr) {
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        pixels_.clear();
        return false;
    }

    width_ = loadedWidth;
    height_ = loadedHeight;
    channels_ = 3;

    const std::size_t pixelCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * static_cast<std::size_t>(channels_);
    pixels_.assign(data, data + pixelCount);
    stbi_image_free(data);
    return true;
}

// 创建棋盘格纹理，作为缺省贴图或调试贴图使用。传入尺寸、格子边长和两种颜色，函数会覆盖当前像素数据。
void Texture2D::createCheckerboard(
    int width,
    int height,
    int tileSize,
    const glm::vec3& colorA,
    const glm::vec3& colorB)
{
    width_ = std::max(width, 2);
    height_ = std::max(height, 2);
    channels_ = 3;
    tileSize = std::max(tileSize, 1);

    pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * static_cast<std::size_t>(channels_));

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const bool useA = (((x / tileSize) + (y / tileSize)) % 2) == 0;
            const glm::vec3 color = useA ? colorA : colorB;
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 3U;
            pixels_[index + 0] = toByte(color.r);
            pixels_[index + 1] = toByte(color.g);
            pixels_[index + 2] = toByte(color.b);
        }
    }
}

// 读取指定像素点颜色，并进行边界钳制与 8-bit 到浮点归一化。供双线性采样过程调用；若纹理无效则返回洋红色告警值。
glm::vec3 Texture2D::texel(int x, int y) const
{
    if (!valid()) {
        return glm::vec3(1.0f, 0.0f, 1.0f);
    }

    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);

    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 3U;
    return glm::vec3(
        static_cast<float>(pixels_[index + 0]) / 255.0f,
        static_cast<float>(pixels_[index + 1]) / 255.0f,
        static_cast<float>(pixels_[index + 2]) / 255.0f);
}

    // 按 UV 坐标执行双线性过滤采样，输出平滑颜色。u 方向环绕、v 方向钳制，适合在光栅化阶段按片段 UV 取色。
glm::vec3 Texture2D::sample(float u, float v) const
{
    if (!valid()) {
        return glm::vec3(1.0f, 0.0f, 1.0f);
    }

    const float wrappedU = Fract(u);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);

    const float x = wrappedU * static_cast<float>(width_ - 1);
    const float y = clampedV * static_cast<float>(height_ - 1);

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const glm::vec3 c00 = texel(x0, y0);
    const glm::vec3 c10 = texel(x1, y0);
    const glm::vec3 c01 = texel(x0, y1);
    const glm::vec3 c11 = texel(x1, y1);

    const glm::vec3 c0 = glm::mix(c00, c10, tx);
    const glm::vec3 c1 = glm::mix(c01, c11, tx);
    return glm::mix(c0, c1, ty);
}

// 从目录中按常见命名约定加载天空盒六个面纹理，要求六面均存在且尺寸一致。
bool CubemapTexture::loadFromDirectory(const std::string& directoryPath)
{
    namespace fs = std::filesystem;

    valid_ = false;
    faceWidth_ = 0;
    faceHeight_ = 0;
    directoryPath_.clear();

    const fs::path rootPath(directoryPath);
    std::error_code ec;
    if (!fs::exists(rootPath, ec) || ec || !fs::is_directory(rootPath, ec) || ec) {
        return false;
    }

    const std::unordered_map<std::string, fs::path> stemPathMap = BuildStemPathMap(rootPath);
    if (stemPathMap.empty()) {
        return false;
    }

    const std::array<std::array<const char*, 3>, 6> faceAliases = {{
        {{"posx", "right", "px"}},
        {{"negx", "left", "nx"}},
        {{"posy", "top", "py"}},
        {{"negy", "bottom", "ny"}},
        {{"posz", "front", "pz"}},
        {{"negz", "back", "nz"}}
    }};

    std::array<fs::path, 6> facePaths;
    for (std::size_t faceIndex = 0; faceIndex < faceAliases.size(); ++faceIndex) {
        bool found = false;
        for (const char* alias : faceAliases[faceIndex]) {
            const auto it = stemPathMap.find(alias);
            if (it == stemPathMap.end()) {
                continue;
            }
            facePaths[faceIndex] = it->second;
            found = true;
            break;
        }
        if (!found) {
            return false;
        }
    }

    for (std::size_t faceIndex = 0; faceIndex < facePaths.size(); ++faceIndex) {
        Texture2D faceTexture;
        if (!faceTexture.loadFromFile(facePaths[faceIndex].string(), false)) {
            return false;
        }

        if (faceIndex == 0) {
            faceWidth_ = faceTexture.width();
            faceHeight_ = faceTexture.height();
        } else if (faceTexture.width() != faceWidth_ || faceTexture.height() != faceHeight_) {
            return false;
        }

        faces_[faceIndex] = std::move(faceTexture);
    }

    directoryPath_ = rootPath.string();
    valid_ = true;
    return true;
}

// 根据方向向量选择立方体贴图采样面，并计算面内局部 uv。
CubemapTexture::Face CubemapTexture::selectFace(const glm::vec3& direction, float& outU, float& outV)
{
    const glm::vec3 absDir = glm::abs(direction);
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        if (direction.x >= 0.0f) {
            outU = -direction.z / absDir.x;
            outV = -direction.y / absDir.x;
            return Face::PositiveX;
        }
        outU = direction.z / absDir.x;
        outV = -direction.y / absDir.x;
        return Face::NegativeX;
    }

    if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        if (direction.y >= 0.0f) {
            outU = direction.x / absDir.y;
            outV = direction.z / absDir.y;
            return Face::PositiveY;
        }
        outU = direction.x / absDir.y;
        outV = -direction.z / absDir.y;
        return Face::NegativeY;
    }

    if (direction.z >= 0.0f) {
        outU = direction.x / absDir.z;
        outV = -direction.y / absDir.z;
        return Face::PositiveZ;
    }
    outU = -direction.x / absDir.z;
    outV = -direction.y / absDir.z;
    return Face::NegativeZ;
}

// 使用世界方向向量进行天空盒采样，用于背景绘制与反射查询等场景。
glm::vec3 CubemapTexture::sample(const glm::vec3& direction) const
{
    if (!valid_) {
        return glm::vec3(1.0f, 0.0f, 1.0f);
    }

    glm::vec3 safeDirection = direction;
    if (glm::dot(safeDirection, safeDirection) <= 1e-12f) {
        safeDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        safeDirection = glm::normalize(safeDirection);
    }

    float localU = 0.0f;
    float localV = 0.0f;
    const Face face = selectFace(safeDirection, localU, localV);

    const float u = std::clamp((localU + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float v = std::clamp((localV + 1.0f) * 0.5f, 0.0f, 1.0f);
    return faces_[static_cast<std::size_t>(face)].sample(u, v);
}
