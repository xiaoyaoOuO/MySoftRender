#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <glm/vec3.hpp>
#include <vector>
#include "Scene.h"
#include "Rasterizer.h"
#include <thread>

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

    // 查询背面剔除状态。用于调试输出或 UI 显示当前剔除开关。
    bool backfaceCullingEnabled() const;

    // 启用或关闭背面剔除。传入 true/false 控制是否过滤背向三角形。
    void setBackfaceCullingEnabled(bool enabled);

    // 切换背面剔除状态。建议绑定快捷键，在运行时快速验证性能与视觉差异。
    void toggleBackfaceCulling();

    // 获取当前 MSAA 档位（1/2/4）。用于状态栏或日志打印当前采样数。
    int msaaSampleCount() const;

    // 设置 MSAA 档位。支持 1/2/4，常用于提供性能质量切换。
    void setMsaaSampleCount(int sampleCount);

    bool wireframeOverlayEnabled() const;
    void setWireframeOverlayEnabled(bool enabled);
    void toggleWireframeOverlay();
    void DrawScene(const Scene& scene);
    void SetFragmentShader(std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)> shader) {
        fragmentShader_ = std::move(shader);
    }

    void rasterizeLocalTriangle(const glm::mat4& model,const glm::mat4& mvp,const std::array<Vertex, 3>& localVertices,const Texture2D* texture = nullptr);
    bool projectLocalTriangleToScreen(const glm::mat4& mvp,std::array<Vertex, 3>& vertices);

    static std::uint32_t packColor(const Color& color);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;

    int threadCount;
    std::vector<std::thread> workerThreads;

    Rasterizer rasterizer_;
    std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)> fragmentShader_;
private:
    void putPixel(int x, int y, const Color& color);
    void putPixel(size_t index, const Color& color);
};
