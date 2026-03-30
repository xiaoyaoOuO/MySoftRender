#include <SDL2/SDL.h>
#include "Scene.h"
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Cube.h"
#include "software_renderer.h"
#include <iostream>
#include <memory>
#include <cstdio>

namespace Window{
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 540;
    constexpr bool kEnablePresentVSync = false;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Scene scene;
    scene.camera = std::make_unique<Camera>();
    scene.camera->setAspectRatio(static_cast<float>(Window::kWindowWidth) / static_cast<float>(Window::kWindowHeight));
    scene.objects.emplace_back(std::make_unique<Triangle>());
    
    Triangle* triangle = new Triangle();
    triangle->setPosition(glm::vec3(-0.5f, 0.0f, -1.0f));
    scene.objects.emplace_back(std::move(triangle));

    SoftwareRenderer renderer(Window::kWindowWidth, Window::kWindowHeight);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "mySoftRender",
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

    const Uint32 rendererFlags = SDL_RENDERER_ACCELERATED
        | (Window::kEnablePresentVSync ? SDL_RENDERER_PRESENTVSYNC : 0u);
    SDL_Renderer* presentRenderer = SDL_CreateRenderer(window, -1, rendererFlags);
    if (presentRenderer == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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

    bool running = true;
    const Uint64 perfFrequency = SDL_GetPerformanceFrequency();
    Uint64 fpsWindowStartCounter = SDL_GetPerformanceCounter();
    int fpsFrameCount = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }
        renderer.clear({ 0, 0, 0, 255 });
        renderer.DrawScene(scene);

        if (SDL_UpdateTexture(
                framebufferTexture,
                nullptr,
                renderer.colorBuffer(),
                renderer.width() * static_cast<int>(sizeof(std::uint32_t)))
            != 0) {
            std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
            break;
        }

        SDL_RenderClear(presentRenderer);
        SDL_RenderCopy(presentRenderer, framebufferTexture, nullptr, nullptr);
        SDL_RenderPresent(presentRenderer);

        ++fpsFrameCount;
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const double elapsedSeconds = static_cast<double>(nowCounter - fpsWindowStartCounter) / static_cast<double>(perfFrequency);
        if (elapsedSeconds >= 0.5) {
            const double fps = static_cast<double>(fpsFrameCount) / elapsedSeconds;
            char title[128];
            std::snprintf(title, sizeof(title), "mySoftRender - FPS: %.1f", fps);
            SDL_SetWindowTitle(window, title);

            fpsFrameCount = 0;
            fpsWindowStartCounter = nowCounter;
        }
    }

    SDL_DestroyTexture(framebufferTexture);
    SDL_DestroyRenderer(presentRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
