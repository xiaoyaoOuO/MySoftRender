#pragma once

#include <cstdint>
#include <vector>

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
    void drawTriangle(Vec2 v0, Vec2 v1, Vec2 v2, const Color& color);

    const std::uint32_t* colorBuffer() const;
    int width() const;
    int height() const;

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;

    static std::uint32_t packColor(const Color& color);
    void putPixel(int x, int y, const Color& color);
};
