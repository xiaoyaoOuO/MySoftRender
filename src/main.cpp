#include <SDL2/SDL.h>
#include "Scene.h"
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Cube.h"
#include "Sphere.h"
#include "software_renderer.h"
#include "ObjLoader.h"
#include <iostream>
#include <memory>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace Window{
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 540;
    constexpr bool kEnablePresentVSync = false;
}

namespace {
std::string ResolveSphereTexturePath(const char* argv0)
{
    namespace fs = std::filesystem;

    const fs::path relativeAssetPath = fs::path("assets") / "earth.jpg";
    std::vector<fs::path> candidates;
    candidates.emplace_back(relativeAssetPath);
    candidates.emplace_back(fs::path("..") / relativeAssetPath);
    candidates.emplace_back(fs::path("../..") / relativeAssetPath);

    if (argv0 != nullptr && argv0[0] != '\0') {
        const fs::path exePath = fs::absolute(fs::path(argv0));
        const fs::path exeDir = exePath.parent_path();
        candidates.emplace_back(exeDir / relativeAssetPath);
        candidates.emplace_back(exeDir.parent_path() / relativeAssetPath);
    }

    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            return candidate.string();
        }
    }

    return relativeAssetPath.string();
}
}

void CreateScene(Scene& scene)
{
    scene.camera = std::make_unique<Camera>();
    scene.camera->setAspectRatio(static_cast<float>(Window::kWindowWidth) / static_cast<float>(Window::kWindowHeight));
    scene.objects.emplace_back(std::make_unique<Triangle>());
    
    // auto triangle = std::make_unique<Triangle>();
    // triangle->setPosition(glm::vec3(-0.5f, 0.0f, -1.0f));
    // scene.objects.emplace_back(std::move(triangle));

    auto sphere = std::make_unique<Sphere>(0.45f, 2, glm::vec3(1.0f, 1.0f, 1.0f));
    sphere->setPosition(glm::vec3(0.8f, 0.0f, -1.2f));
    scene.objects.emplace_back(std::move(sphere));

    // auto cube = std::make_unique<Cube>(glm::vec3(0.0f, 0.0f, -1.5f), glm::vec3(1.0f), glm::vec4(1.0f, 0.8f, 0.8f, 1.0f));
    // cube->setPosition(glm::vec3(-0.8f, 0.0f, -1.5f));
    // scene.objects.emplace_back(std::move(cube));

    // ObjMeshData MaryMesh;
    // const bool ok = ObjLoader::LoadFromFile("assets/teapot.obj", MaryMesh);
    // if (!ok || MaryMesh.empty()) {
    //     std::cerr << "Failed to load teapot.obj" << std::endl;
    //     return;
    // }
    // for(const auto& vertex : MaryMesh.vertices) {
    //     Triangle* tri = new Triangle();
    //     // tri->setVertexs({
    //     //     glm::vec4(vertex.position, 1.0f),
    //     //     glm::vec4(vertex.position, 1.0f),
    //     //     glm::vec4(vertex.position, 1.0f)
    //     // });
        
    // }

}

int main(int argc, char* argv[])
{
    Scene scene;
    CreateScene(scene);

    SoftwareRenderer renderer(Window::kWindowWidth, Window::kWindowHeight);
    const std::string sphereTexturePath = ResolveSphereTexturePath(argc > 0 ? argv[0] : nullptr);
    if (!renderer.loadSphereTexture(sphereTexturePath)) {
        std::cout << "Sphere texture: using generated checkerboard (texture not found)" << '\n';
    } else {
        std::cout << "Sphere texture loaded from: " << sphereTexturePath << '\n';
    }
    std::cout << "Wireframe overlay: OFF (press F1 to toggle)" << '\n';

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
    Uint64 lastFrameCounter = fpsWindowStartCounter;
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
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_F1) {
                renderer.toggleWireframeOverlay();
                std::cout << "Wireframe overlay: "
                          << (renderer.wireframeOverlayEnabled() ? "ON" : "OFF")
                          << " (press F1 to toggle)" << '\n';
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
        {
            const float deltaTime = static_cast<float>(nowCounter - lastFrameCounter) / static_cast<float>(perfFrequency);
            lastFrameCounter = nowCounter;
            scene.RotateObjects(deltaTime);
        }
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
