#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>
#include "Scene.h"

struct Vec2 {
    float x;
    float y;
};

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

class SoftwareRenderer {
public:
    SoftwareRenderer(int width, int height);

    void clear(const Color& color);
    void drawTriangle(const std::vector<Vec2>& vertexs, const std::vector<glm::vec3>& colors);

    const std::uint32_t* colorBuffer() const;
    int width() const;
    int height() const;
    void DrawScene(const Scene& scene);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;

    static std::uint32_t packColor(const Color& color);
    void putPixel(int x, int y, const Color& color);
};
