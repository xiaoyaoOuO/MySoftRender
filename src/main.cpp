#include <SDL2/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <stb_image.h>

#include "software_renderer.h"

namespace Window{
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 540;
}

int main(int argc, char* argv[])
{
    // 步骤1：初始化 SDL 的视频与事件子系统。
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    // 步骤2：创建操作系统窗口，用于显示最终画面。
    SDL_Window* window = SDL_CreateWindow(
        "mySoftRender - CPU Rasterizer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        Window::kWindowWidth,
        Window::kWindowHeight,
        SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    // 步骤3：创建 SDL 渲染器，仅用于把 CPU 帧缓冲呈现到窗口。
    SDL_Renderer* presentRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (presentRenderer == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 步骤4：创建可流式更新的纹理，每帧接收 CPU 生成的像素数据。
    SDL_Texture* framebufferTexture = SDL_CreateTexture(
        presentRenderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        Window::kWindowWidth,
        Window::kWindowHeight);

    if (framebufferTexture == nullptr) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(presentRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 步骤5：初始化 Dear ImGui 上下文和基础样式。
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    // 步骤6：绑定 ImGui 的 SDL2 事件输入与 SDL_Renderer 绘制后端。
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, presentRenderer)) {
        std::cerr << "ImGui SDL2 backend init failed\n";
        ImGui::DestroyContext();
        SDL_DestroyTexture(framebufferTexture);
        SDL_DestroyRenderer(presentRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!ImGui_ImplSDLRenderer2_Init(presentRenderer)) {
        std::cerr << "ImGui SDL_Renderer backend init failed\n";
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyTexture(framebufferTexture);
        SDL_DestroyRenderer(presentRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 步骤7：创建软渲染器对象，内部维护 CPU 颜色缓冲。
    SoftwareRenderer softwareRenderer(Window::kWindowWidth, Window::kWindowHeight);

    // 可选步骤：从 argv[1] 加载纹理，用于验证 stb_image 集成是否正常。
    int loadedTextureWidth = 0;
    int loadedTextureHeight = 0;
    int loadedTextureChannels = 0;
    std::vector<stbi_uc> loadedTexturePixels;

    if (argc > 1) {
        stbi_uc* textureData = stbi_load(argv[1], &loadedTextureWidth, &loadedTextureHeight, &loadedTextureChannels, 4);
        if (textureData != nullptr) {
            const std::size_t pixelCount = static_cast<std::size_t>(loadedTextureWidth)
                * static_cast<std::size_t>(loadedTextureHeight)
                * 4U;
            loadedTexturePixels.assign(textureData, textureData + pixelCount);
            stbi_image_free(textureData);
            loadedTextureChannels = 4;
        } else {
            std::cerr << "stb_image load failed for '" << argv[1] << "': " << stbi_failure_reason() << '\n';
        }
    }

    glm::vec4 clearColor(17.0f / 255.0f, 20.0f / 255.0f, 28.0f / 255.0f, 1.0f);
    bool showDemoWindow = false;

    // 步骤8：进入实时渲染主循环。
    bool running = true;
    const auto start = std::chrono::steady_clock::now();

    while (running) {
        // 步骤8.1：处理系统与输入事件。
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(now - start).count();

        // 步骤8.2：开始新的 ImGui 帧。
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // 步骤8.3：计算动画背景色（浮点），并转换为 8 位 RGBA。
        const float pulse = 0.5f + 0.5f * std::sin(t * 1.4f);
        const glm::vec4 animatedColor = glm::mix(clearColor, glm::vec4(0.09f, 0.11f, 0.16f, 1.0f), pulse * 0.25f);
        const Color clearColor8{
            static_cast<std::uint8_t>(glm::clamp(animatedColor.r, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(animatedColor.g, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(animatedColor.b, 0.0f, 1.0f) * 255.0f),
            255U,
        };

        // 步骤8.4：在 CPU 侧清空软渲染帧缓冲。
        softwareRenderer.clear(clearColor8);

        // 步骤8.5：构建调试 UI，展示运行状态并提供交互控件。
        ImGui::Begin("Renderer Debug");
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::ColorEdit3("Clear Color", &clearColor.x);
        ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);

        if (argc > 1) {
            if (loadedTexturePixels.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Texture load failed: %s", argv[1]);
            } else {
                ImGui::Text("Loaded texture: %dx%d RGBA", loadedTextureWidth, loadedTextureHeight);
            }
        } else {
            ImGui::TextUnformatted("Pass an image path as argv[1] to test stb_image loading.");
        }

        ImGui::End();

        if (showDemoWindow) {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }

        // 步骤8.6：把 CPU 缓冲上传到纹理，再呈现纹理与 ImGui 绘制数据。
        if (SDL_UpdateTexture(
                framebufferTexture,
                nullptr,
                softwareRenderer.colorBuffer(),
                softwareRenderer.width() * static_cast<int>(sizeof(std::uint32_t)))
            != 0) {
            std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
            break;
        }

        SDL_RenderClear(presentRenderer);
        SDL_RenderCopy(presentRenderer, framebufferTexture, nullptr, nullptr);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), presentRenderer);

        SDL_RenderPresent(presentRenderer);
    }

    // 步骤9：按创建的逆序释放 ImGui 与 SDL 资源。
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyTexture(framebufferTexture);
    SDL_DestroyRenderer(presentRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
