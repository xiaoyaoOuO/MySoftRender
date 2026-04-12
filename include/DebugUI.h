#pragma once

#include <SDL2/SDL.h>

class Scene;

// 作用：封装 ImGui 调试界面的生命周期与绘制流程，避免把 UI 细节耦合到主循环。
// 用法：主程序按 initialize -> processEvent -> beginFrame -> drawShadowPanel -> render 的顺序调用。
class DebugUI
{
public:
    DebugUI() = default;
    ~DebugUI();

    // 作用：初始化 ImGui 上下文与 SDL2/SDLRenderer2 后端。
    // 用法：在 SDL_Window/SDL_Renderer 创建成功后调用一次，返回 true 表示可用。
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);

    // 作用：释放 ImGui 后端与上下文资源。
    // 用法：程序退出前调用一次，或由析构函数自动兜底调用。
    void shutdown();

    // 作用：把 SDL 事件转发给 ImGui 后端。
    // 用法：在主事件循环中每个 SDL_Event 都调用一次。
    void processEvent(const SDL_Event& event);

    // 作用：开始新的 ImGui 帧。
    // 用法：每帧绘制 UI 前调用一次。
    void beginFrame();

    // 作用：绘制调试面板（阴影参数、光源参数、模型位姿）。
    // 用法：每帧在 beginFrame 之后调用，可实时修改 Scene 内各项渲染与场景参数。
    void drawShadowPanel(Scene& scene);

    // 作用：提交并渲染 ImGui 绘制数据。
    // 用法：在 SDL_RenderCopy 完成后、SDL_RenderPresent 前调用。
    void render();

    bool isInitialized() const { return initialized_; }
    bool isVisible() const { return visible_; }
    void toggleVisible() { visible_ = !visible_; }

    // 作用：查询 ImGui 是否希望接管当前帧鼠标输入。
    // 用法：主程序可据此屏蔽相机转向等业务鼠标逻辑。
    bool wantsMouseCapture() const;

    // 作用：查询 ImGui 是否希望接管当前帧键盘输入。
    // 用法：主程序可据此屏蔽 WASD/F 键等业务键位逻辑。
    bool wantsKeyboardCapture() const;

private:
    SDL_Renderer* renderer_ = nullptr;
    bool initialized_ = false;
    bool visible_ = true;
    int selectedObjectIndex_ = 0;
};
