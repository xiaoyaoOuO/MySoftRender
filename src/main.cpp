#include <SDL2/SDL.h>
#include "Scene.h"
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Cube.h"
#include "Sphere.h"
#include "MeshObject.h"
#include "software_renderer.h"
#include "ObjLoader.h"
#include "Texture.h"
#include <iostream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace Window{
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 540;
    constexpr bool kEnablePresentVSync = false;
}

namespace {
// 作用：根据可执行文件位置和相对资源路径，解析出可用的资源文件路径。
// 用法：传入 argv0 与如 assets/... 的相对路径，函数会在常见运行目录层级中自动查找。
std::string ResolveAssetPath(const char* argv0, const std::filesystem::path& relativeAssetPath)
{
    namespace fs = std::filesystem;

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

// 作用：解析球体纹理路径（earth.jpg）。
// 用法：程序启动时调用，得到可直接传给 Texture2D::loadFromFile 的路径。
std::string ResolveSphereTexturePath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "earth.jpg");
}

// 作用：解析 Mary 模型纹理路径（MC003_Kozakura_Mari.png）。
// 用法：程序启动时调用，得到 Mary 贴图文件的可用路径。
std::string ResolveMaryTexturePath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "mary" / "MC003_Kozakura_Mari.png");
}

// 作用：解析 Mary 模型几何文件路径（mary.obj）。
// 用法：创建场景前调用，得到 OBJ 文件路径用于加载网格。
std::string ResolveMaryObjPath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "mary" / "mary.obj");
}

// 作用：按 1x -> 2x -> 4x -> 1x 的顺序切换 MSAA 档位。
// 用法：每次按键触发时传入当前档位，返回下一个可用档位。
int NextMsaaSampleCount(int currentSampleCount)
{
    if (currentSampleCount <= 1) {
        return 2;
    }
    if (currentSampleCount <= 2) {
        return 4;
    }
    return 1;
}

// 作用：根据事件驱动维护的按键状态，执行 FPS 相机 WASD 平面移动。
// 用法：主循环中持续传入当前 W/A/S/D 是否按下与 deltaTime，即可得到稳定的连续移动。
void MoveCameraWithInput(
    Camera& camera,
    bool moveForward,
    bool moveBackward,
    bool moveLeft,
    bool moveRight,
    float deltaTime)
{
    // 作用：将相机前向向量投影到水平面，构建 FPS 常见的“只在地面平移”的移动基。
    // 用法：每帧传入按键布尔状态与 deltaTime，即可得到与帧率无关的 WASD 位移。
    const glm::vec3 rawForward = camera.target() - camera.position();
    if (glm::dot(rawForward, rawForward) <= 1e-8f) {
        return;
    }

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 forwardOnGround(rawForward.x, 0.0f, rawForward.z);
    if (glm::dot(forwardOnGround, forwardOnGround) <= 1e-8f) {
        forwardOnGround = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    forwardOnGround = glm::normalize(forwardOnGround);
    const glm::vec3 rightOnGround = glm::normalize(glm::cross(forwardOnGround, worldUp));

    const float moveSpeed = camera.speed() * deltaTime;
    glm::vec3 moveDelta(0.0f);

    if (moveForward) {
        moveDelta += forwardOnGround * moveSpeed;
    }
    if (moveBackward) {
        moveDelta -= forwardOnGround * moveSpeed;
    }
    if (moveLeft) {
        moveDelta -= rightOnGround * moveSpeed;
    }
    if (moveRight) {
        moveDelta += rightOnGround * moveSpeed;
    }

    if (glm::dot(moveDelta, moveDelta) > 0.0f) {
        camera.move(moveDelta);
    }
}

// 作用：从当前相机朝向反解出 yaw/pitch 角，便于鼠标视角控制从现有视角连续接管。
// 用法：场景初始化完成后调用一次，得到可直接用于 FPS 鼠标控制的初始角度。
void ExtractYawPitchFromCamera(const Camera& camera, float& yawDeg, float& pitchDeg)
{
    const glm::vec3 rawForward = camera.target() - camera.position();
    if (glm::dot(rawForward, rawForward) <= 1e-8f) {
        yawDeg = 0.0f;
        pitchDeg = 0.0f;
        return;
    }

    const glm::vec3 forward = glm::normalize(rawForward);
    pitchDeg = glm::degrees(std::asin(forward.y));
    yawDeg = glm::degrees(std::atan2(forward.x, -forward.z));
}

// 作用：根据 yaw/pitch 角重建相机前向与上方向，形成标准 FPS 视角。
// 用法：在鼠标移动后更新 yaw/pitch 并调用该函数，即可实现稳定的水平转向与俯仰。
void ApplyYawPitchToCamera(Camera& camera, float yawDeg, float pitchDeg)
{
    const float yawRad = glm::radians(yawDeg);
    const float pitchRad = glm::radians(pitchDeg);

    glm::vec3 forward;
    forward.x = std::sin(yawRad) * std::cos(pitchRad);
    forward.y = std::sin(pitchRad);
    forward.z = -std::cos(yawRad) * std::cos(pitchRad);

    if (glm::dot(forward, forward) <= 1e-8f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(forward, worldUp);
    if (glm::dot(right, right) <= 1e-8f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(right);
    }
    const glm::vec3 cameraUp = glm::normalize(glm::cross(right, forward));

    camera.setTarget(camera.position() + forward);
    camera.setUp(cameraUp);
}
}

// 作用：构建场景对象，并将纹理按对象维度绑定到 Mary 网格对象上。
// 用法：调用时传入已加载好的共享纹理和 OBJ 路径，后续渲染按单网格对象统一遍历索引绘制。
void CreateScene(
    Scene& scene,
    const std::shared_ptr<Texture2D>& sphereTexture,
    const std::shared_ptr<Texture2D>& maryTexture,
    const std::string& maryObjPath)
{
    scene.camera = std::make_unique<Camera>();
    scene.camera->setAspectRatio(static_cast<float>(Window::kWindowWidth) / static_cast<float>(Window::kWindowHeight));
    // scene.objects.emplace_back(std::make_unique<Triangle>());
    
    // auto triangle = std::make_unique<Triangle>();
    // triangle->setPosition(glm::vec3(-0.5f, 0.0f, -1.0f));
    // scene.objects.emplace_back(std::move(triangle));

    // auto sphere = std::make_unique<Sphere>(0.45f, 2, glm::vec3(1.0f, 1.0f, 1.0f));
    // sphere->setPosition(glm::vec3(0.8f, 0.0f, -1.2f));
    // sphere->setTexture(sphereTexture);
    // scene.objects.emplace_back(std::move(sphere));

    // auto cube = std::make_unique<Cube>(glm::vec3(0.0f, 0.0f, -1.5f), glm::vec3(1.0f), glm::vec4(1.0f, 0.8f, 0.8f, 1.0f));
    // cube->setPosition(glm::vec3(-0.8f, 0.0f, -1.5f));
    // scene.objects.emplace_back(std::move(cube));

    ObjMeshData MaryMesh;
    const bool ok = ObjLoader::LoadFromFile(maryObjPath, MaryMesh);
    if (!ok || MaryMesh.empty()) {
        std::cerr << "Failed to load mary.obj: " << maryObjPath << std::endl;
        return;
    }

    (void)sphereTexture;

    auto maryObject = std::make_unique<MeshObject>(MaryMesh);
    maryObject->setPosition(glm::vec3(0.0f, -1.0f, -2.0f));
    maryObject->setTexture(maryTexture);
    scene.objects.emplace_back(std::move(maryObject));

}

int main(int argc, char* argv[])
{
    const char* argv0 = argc > 0 ? argv[0] : nullptr;
    const std::string sphereTexturePath = ResolveSphereTexturePath(argv0);
    const std::string maryTexturePath = ResolveMaryTexturePath(argv0);
    const std::string maryObjPath = ResolveMaryObjPath(argv0);

    auto sphereTexture = std::make_shared<Texture2D>();
    if (!sphereTexture->loadFromFile(sphereTexturePath)) {
        sphereTexture->createCheckerboard(
            1024,
            512,
            32,
            glm::vec3(0.95f, 0.95f, 0.95f),
            glm::vec3(0.12f, 0.38f, 0.78f));
        std::cout << "Sphere texture: using generated checkerboard (texture not found)" << '\n';
    } else {
        std::cout << "Sphere texture loaded from: " << sphereTexturePath << '\n';
    }

    std::shared_ptr<Texture2D> maryTexture = std::make_shared<Texture2D>();
    if (!maryTexture->loadFromFile(maryTexturePath)) {
        maryTexture.reset();
        std::cout << "Mary texture: not found, rendering with vertex color" << '\n';
    } else {
        std::cout << "Mary texture loaded from: " << maryTexturePath << '\n';
    }

    Scene scene;
    CreateScene(scene, sphereTexture, maryTexture, maryObjPath);

    SoftwareRenderer renderer(Window::kWindowWidth, Window::kWindowHeight);
    renderer.setBackfaceCullingEnabled(true);
    renderer.setMsaaSampleCount(1);
    std::cout << "Wireframe overlay: OFF (press F1 to toggle)" << '\n';
    std::cout << "Back-face culling: ON (press F2 to toggle)" << '\n';
    std::cout << "MSAA samples: " << renderer.msaaSampleCount() << "x (press F3 to cycle 1x/2x/4x)" << '\n';

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
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;

    float cameraYawDeg = 0.0f;
    float cameraPitchDeg = 0.0f;
    constexpr float kMouseSensitivityDegPerPixel = 0.12f;
    constexpr float kMaxPitchDeg = 89.0f;
    bool mouseCaptureEnabled = true;

    if (scene.camera) {
        ExtractYawPitchFromCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
        ApplyYawPitchToCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
    }

    // 作用：启用相对鼠标模式，用鼠标位移驱动 FPS 视角旋转。
    // 用法：进入渲染循环前开启；失焦时会临时关闭，回到窗口时自动恢复。
    if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
        std::cerr << "SDL_SetRelativeMouseMode failed: " << SDL_GetError() << '\n';
        mouseCaptureEnabled = false;
    } else {
        SDL_SetWindowGrab(window, SDL_TRUE);
        SDL_ShowCursor(SDL_DISABLE);
        std::cout << "Mouse look: ON (move mouse to yaw/pitch)" << '\n';
    }

    while (running) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const float deltaTime = static_cast<float>(nowCounter - lastFrameCounter) / static_cast<float>(perfFrequency);
        lastFrameCounter = nowCounter;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            // 作用：将鼠标相对位移转换为 FPS 相机的 yaw/pitch 变化。
            // 用法：保持窗口聚焦并移动鼠标，即可水平转向与上下俯仰。
            if (event.type == SDL_MOUSEMOTION && mouseCaptureEnabled && scene.camera) {
                cameraYawDeg += static_cast<float>(event.motion.xrel) * kMouseSensitivityDegPerPixel;
                cameraPitchDeg -= static_cast<float>(event.motion.yrel) * kMouseSensitivityDegPerPixel;

                if (cameraPitchDeg > kMaxPitchDeg) {
                    cameraPitchDeg = kMaxPitchDeg;
                }
                if (cameraPitchDeg < -kMaxPitchDeg) {
                    cameraPitchDeg = -kMaxPitchDeg;
                }

                ApplyYawPitchToCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
            }

            // 作用：用 KEYDOWN/KEYUP 维护 WASD 持续按下状态，避免依赖全局键盘状态数组。
            // 用法：按下时置 true，抬起时置 false，后续移动逻辑直接读取该状态。
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const bool isKeyDown = (event.type == SDL_KEYDOWN);
                const SDL_Keycode sym = event.key.keysym.sym;
                switch (sym) {
                case SDLK_w:
                    moveForward = isKeyDown;
                    break;
                case SDLK_s:
                    moveBackward = isKeyDown;
                    break;
                case SDLK_a:
                    moveLeft = isKeyDown;
                    break;
                case SDLK_d:
                    moveRight = isKeyDown;
                    break;
                default:
                    break;
                }
            }

            // 作用：功能键使用 KEYDOWN 的边沿触发（repeat==0），避免连续触发。
            // 用法：单击一次 F1/F2/F3/ESC，只会触发一次状态切换或退出。
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_F1:
                    renderer.toggleWireframeOverlay();
                    std::cout << "Wireframe overlay: "
                              << (renderer.wireframeOverlayEnabled() ? "ON" : "OFF")
                              << " (press F1 to toggle)" << '\n';
                    break;
                case SDLK_F2:
                    renderer.toggleBackfaceCulling();
                    std::cout << "Back-face culling: "
                              << (renderer.backfaceCullingEnabled() ? "ON" : "OFF")
                              << " (press F2 to toggle)" << '\n';
                    break;
                case SDLK_F3:
                    renderer.setMsaaSampleCount(NextMsaaSampleCount(renderer.msaaSampleCount()));
                    std::cout << "MSAA samples: "
                              << renderer.msaaSampleCount()
                              << "x (press F3 to cycle 1x/2x/4x)" << '\n';
                    break;
                default:
                    break;
                }
            }

            // 作用：窗口焦点切换时同步鼠标捕获状态，并重置移动状态。
            // 用法：失焦时释放鼠标，聚焦时恢复相对模式，避免输入卡键与鼠标漂移。
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    moveForward = false;
                    moveBackward = false;
                    moveLeft = false;
                    moveRight = false;

                    if (mouseCaptureEnabled) {
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_SetWindowGrab(window, SDL_FALSE);
                        SDL_ShowCursor(SDL_ENABLE);
                    }
                }

                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    if (mouseCaptureEnabled) {
                        if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
                            SDL_SetWindowGrab(window, SDL_TRUE);
                            SDL_ShowCursor(SDL_DISABLE);
                        }
                    }
                }
            }
        }

        if (!running) {
            break;
        }

        if (scene.camera) {
            MoveCameraWithInput(
                *scene.camera,
                moveForward,
                moveBackward,
                moveLeft,
                moveRight,
                deltaTime);
        }

        scene.RotateObjects(deltaTime);

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
        const Uint64 fpsCounter = SDL_GetPerformanceCounter();
        const double elapsedSeconds = static_cast<double>(fpsCounter - fpsWindowStartCounter) / static_cast<double>(perfFrequency);
        if (elapsedSeconds >= 0.5) {
            const double fps = static_cast<double>(fpsFrameCount) / elapsedSeconds;
            char title[128];
            std::snprintf(title, sizeof(title), "mySoftRender - FPS: %.1f", fps);
            SDL_SetWindowTitle(window, title);

            fpsFrameCount = 0;
            fpsWindowStartCounter = fpsCounter;
        }
    }

    SDL_DestroyTexture(framebufferTexture);
    SDL_DestroyRenderer(presentRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
