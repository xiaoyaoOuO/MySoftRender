#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <glm/vec3.hpp>
#include <vector>
#include "Scene.h"
#include "Rasterizer.h"
#include "RenderThreadPool.h"

/**
 * @brief 记录片元着色阶段的线程池执行统计信息。
 */
struct FragmentThreadingStats
{
    std::size_t fragmentCount = 0; // 本帧片元总数
    std::size_t poolThreadCount = 0; // 线程池配置线程数
    std::size_t activeWorkerCount = 0; // 当前时刻活跃工作线程数
    std::size_t pendingTaskCount = 0; // 当前时刻线程池排队任务数
    std::size_t scheduledTaskCount = 0; // 本帧提交到线程池的任务数
    std::size_t dispatchedWorkerCount = 0; // 本帧实际参与执行的线程数估计值
};

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

    /**
     * @brief 查询片元着色是否启用多线程。
     * @return 启用返回 true，否则返回 false。
     */
    bool fragmentMultithreadingEnabled() const;

    /**
     * @brief 设置片元着色是否启用多线程。
     * @param enabled true 表示启用线程池并行，false 表示强制单线程执行。
     */
    void setFragmentMultithreadingEnabled(bool enabled);

    /**
     * @brief 获取片元线程池的执行统计快照。
     * @return 返回最近一帧更新的统计信息。
     */
    const FragmentThreadingStats& fragmentThreadingStats() const;

    void rasterizeLocalTriangle(const glm::mat4& model,const glm::mat4& mvp,const std::array<Vertex, 3>& localVertices,const Texture2D* texture = nullptr);
    bool projectLocalTriangleToScreen(const glm::mat4& mvp,std::array<Vertex, 3>& vertices);

    static std::uint32_t packColor(const Color& color);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> colorBuffer_;

    Rasterizer rasterizer_;
    RenderThreadPool fragmentShadingThreadPool_;
    bool fragmentMultithreadingEnabled_ = true;
    FragmentThreadingStats fragmentThreadingStats_;
    std::function<void(std::vector<std::uint32_t>&, const Fragment&, const Scene&)> fragmentShader_;
private:
    void putPixel(int x, int y, const Color& color);
    void putPixel(size_t index, const Color& color);
};
