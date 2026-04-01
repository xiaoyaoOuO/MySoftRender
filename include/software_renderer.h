#pragma once

#include <cstdint>
#include <string>
#include <glm/vec3.hpp>
#include <vector>
#include "Scene.h"
#include "Rasterizer.h"
#include "Texture.h"


/**
 * @brief 软光栅渲染器
 */
class SoftwareRenderer {
public:
    SoftwareRenderer(int width, int height);

    void clear(const Color& color);

    const std::uint32_t* colorBuffer() const;
    int width() const;
    int height() const;
    bool wireframeOverlayEnabled() const;
    void setWireframeOverlayEnabled(bool enabled);
    void toggleWireframeOverlay();
    bool loadSphereTexture(const std::string& texturePath);
    void useDefaultSphereTexture();
    void DrawScene(const Scene& scene);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;
    Texture2D sphereTexture_;

    Rasterizer rasterizer_;
    std::function<void(std::vector<std::uint32_t>&, const Fragment&)> fragmentShader_;
private:
    static std::uint32_t packColor(const Color& color);
    void putPixel(int x, int y, const Color& color);
    void putPixel(size_t index, const Color& color);
};
