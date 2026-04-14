#pragma once

#include <SDL2/SDL.h>

class Scene;
class SoftwareRenderer;

// 封装 ImGui 调试界面的生命周期与绘制流程，避免把 UI 细节耦合到主循环。主程序按 initialize -> processEvent -> beginFrame -> drawShadowPanel -> render 的顺序调用。
class DebugUI
{
public:
    DebugUI() = default;
    ~DebugUI();

    // 初始化 ImGui 上下文与 SDL2/SDLRenderer2 后端。在 SDL_Window/SDL_Renderer 创建成功后调用一次，返回 true 表示可用。
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);

    // 释放 ImGui 后端与上下文资源。程序退出前调用一次，或由析构函数自动兜底调用。
    void shutdown();

    // 把 SDL 事件转发给 ImGui 后端。在主事件循环中每个 SDL_Event 都调用一次。
    void processEvent(const SDL_Event& event);

    // 开始新的 ImGui 帧。每帧绘制 UI 前调用一次。
    void beginFrame();

    // 绘制调试面板（阴影参数、光源参数、模型位姿、线程池状态）。每帧在 beginFrame 之后调用，可实时修改 Scene 与 Renderer 相关参数。
    void drawShadowPanel(Scene& scene, SoftwareRenderer& renderer);

    // 提交并渲染 ImGui 绘制数据。在 SDL_RenderCopy 完成后、SDL_RenderPresent 前调用。
    void render();

    bool isInitialized() const { return initialized_; }
    bool isVisible() const { return visible_; }
    void toggleVisible() { visible_ = !visible_; }

    // 查询 ImGui 是否希望接管当前帧鼠标输入。主程序可据此屏蔽相机转向等业务鼠标逻辑。
    bool wantsMouseCapture() const;

    // 查询 ImGui 是否希望接管当前帧键盘输入。主程序可据此屏蔽 WASD/F 键等业务键位逻辑。
    bool wantsKeyboardCapture() const;

    // 设置当前场景预设索引。主程序在场景切换成功后调用，用于同步下拉框显示状态。
    void setCurrentScenePreset(int presetIndex);

    // 查询是否存在待处理的场景切换请求。返回 true 表示本帧 UI 发起了切换。
    bool hasPendingSceneSwitch() const;

    // 消费并返回一次场景切换请求。无请求时返回 -1。
    int consumePendingSceneSwitch();

private:
    SDL_Renderer* renderer_ = nullptr;
    bool initialized_ = false;
    bool visible_ = true;
    int selectedObjectIndex_ = 0;
    int currentScenePreset_ = 0;
    int pendingScenePreset_ = -1;
};
