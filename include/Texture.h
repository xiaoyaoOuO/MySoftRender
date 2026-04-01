#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

class Texture2D
{
public:
    bool loadFromFile(const std::string& filePath, bool flipVertically = false);

    void createCheckerboard(
        int width,
        int height,
        int tileSize,
        const glm::vec3& colorA = glm::vec3(0.95f, 0.95f, 0.95f),
        const glm::vec3& colorB = glm::vec3(0.15f, 0.35f, 0.75f));

    glm::vec3 sample(float u, float v) const;

    bool valid() const { return width_ > 0 && height_ > 0 && !pixels_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    glm::vec3 texel(int x, int y) const;
    static std::uint8_t toByte(float value);

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<std::uint8_t> pixels_;
};
