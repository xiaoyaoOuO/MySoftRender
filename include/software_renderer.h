#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <glm/vec3.hpp>
#include <vector>
#include "Scene.h"
#include "Rasterizer.h"


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

    // 作用：查询背面剔除状态。
    // 用法：用于调试输出或 UI 显示当前剔除开关。
    bool backfaceCullingEnabled() const;

    // 作用：启用或关闭背面剔除。
    // 用法：传入 true/false 控制是否过滤背向三角形。
    void setBackfaceCullingEnabled(bool enabled);

    // 作用：切换背面剔除状态。
    // 用法：建议绑定快捷键，在运行时快速验证性能与视觉差异。
    void toggleBackfaceCulling();

    // 作用：获取当前 MSAA 档位（1/2/4）。
    // 用法：用于状态栏或日志打印当前采样数。
    int msaaSampleCount() const;

    // 作用：设置 MSAA 档位。
    // 用法：支持 1/2/4，常用于提供性能质量切换。
    void setMsaaSampleCount(int sampleCount);

    bool wireframeOverlayEnabled() const;
    void setWireframeOverlayEnabled(bool enabled);
    void toggleWireframeOverlay();
    void DrawScene(const Scene& scene);
    void SetFragmentShader(std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)> shader) {
        fragmentShader_ = std::move(shader);
    }
    static std::uint32_t packColor(const Color& color);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;


    Rasterizer rasterizer_;
    std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)> fragmentShader_;
private:
    void putPixel(int x, int y, const Color& color);
    void putPixel(size_t index, const Color& color);
};
