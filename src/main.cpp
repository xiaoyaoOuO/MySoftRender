#include <SDL2/SDL.h>
#include "Scene.h"
#include "Camera.h"
#include "Object.h"
#include "Triangle.h"
#include "Cube.h"
#include "Sphere.h"
#include "MeshObject.h"
#include "software_renderer.h"
#include "DebugUI.h"
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
#include <algorithm>

namespace Window{
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 540;
    constexpr bool kEnablePresentVSync = false;
}

namespace {
// 根据可执行文件位置和相对资源路径，解析出可用的资源文件路径。传入 argv0 与如 assets/... 的相对路径，函数会在常见运行目录层级中自动查找。
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

// 解析球体纹理路径（earth.jpg）。程序启动时调用，得到可直接传给 Texture2D::loadFromFile 的路径。
std::string ResolveSphereTexturePath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "earth.jpg");
}

// 解析 Mary 模型纹理路径（MC003_Kozakura_Mari.png）。程序启动时调用，得到 Mary 贴图文件的可用路径。
std::string ResolveMaryTexturePath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "mary" / "MC003_Kozakura_Mari.png");
}

// 解析 Mary 模型几何文件路径（mary.obj）。创建场景前调用，得到 OBJ 文件路径用于加载网格。
std::string ResolveMaryObjPath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "mary" / "mary.obj");
}

std::string ResolveFloorPath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "floor" / "floor.obj");
}

// 按 1x -> 2x -> 4x -> 1x 的顺序切换 MSAA 档位。每次按键触发时传入当前档位，返回下一个可用档位。
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

// 根据事件驱动维护的按键状态，执行 FPS 相机 WASD 平面移动。主循环中持续传入当前 W/A/S/D 是否按下与 deltaTime，即可得到稳定的连续移动。
void MoveCameraWithInput(
    Camera& camera,
    bool moveForward,
    bool moveBackward,
    bool moveLeft,
    bool moveRight,
    float deltaTime)
{
    // 将相机前向向量投影到水平面，构建 FPS 常见的“只在地面平移”的移动基。每帧传入按键布尔状态与 deltaTime，即可得到与帧率无关的 WASD 位移。
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

// 从当前相机朝向反解出 yaw/pitch 角，便于鼠标视角控制从现有视角连续接管。场景初始化完成后调用一次，得到可直接用于 FPS 鼠标控制的初始角度。
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

// 根据 yaw/pitch 角重建相机前向与上方向，形成标准 FPS 视角。在鼠标移动后更新 yaw/pitch 并调用该函数，即可实现稳定的水平转向与俯仰。
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

/**
 * @brief 统一管理鼠标捕获与释放状态，供 FPS 视角和 ImGui 交互切换。
 * @param enabled true 表示开启相对鼠标模式并隐藏光标，false 表示释放光标。
 * @param window SDL 窗口指针，用于设置窗口抓取状态。
 * @param mouseCaptureEnabled 当前是否处于鼠标捕获状态（函数内会更新）。
 * @param mouseCaptureSupported 当前平台是否支持相对鼠标模式（函数内会更新）。
 * @return 无返回值。
 */
void SetMouseCaptureState(
    bool enabled,
    SDL_Window* window,
    bool& mouseCaptureEnabled,
    bool& mouseCaptureSupported)
{
    if (!mouseCaptureSupported) {
        return;
    }

    if (enabled) {
        if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
            SDL_SetWindowGrab(window, SDL_TRUE);
            SDL_ShowCursor(SDL_DISABLE);
            mouseCaptureEnabled = true;
        } else {
            std::cerr << "SDL_SetRelativeMouseMode failed: " << SDL_GetError() << '\n';
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_SetWindowGrab(window, SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            mouseCaptureEnabled = false;
            mouseCaptureSupported = false;
        }
    } else {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_SetWindowGrab(window, SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        mouseCaptureEnabled = false;
    }
}
}

/**
 * @brief 场景预设类型。
 */
enum class ScenePreset
{
    Scene1MaryFloorPoint = 0,
    Scene2DualSphereFloorPoint = 1
};

/**
 * @brief 场景构建所需的共享资源。
 */
struct SceneBuildResources
{
    std::shared_ptr<Texture2D> sphereTexture;
    std::shared_ptr<Texture2D> maryTexture;
    std::string maryObjPath;
    std::string floorObjPath;
};

/**
 * @brief 将场景预设转为 UI 索引。
 */
int ScenePresetToIndex(ScenePreset preset)
{
    return static_cast<int>(preset);
}

/**
 * @brief 将 UI 索引转为场景预设。
 */
ScenePreset ScenePresetFromIndex(int presetIndex)
{
    if (presetIndex == 1) {
        return ScenePreset::Scene2DualSphereFloorPoint;
    }
    return ScenePreset::Scene1MaryFloorPoint;
}

/**
 * @brief 返回场景预设显示名，用于日志输出。
 */
const char* ScenePresetName(ScenePreset preset)
{
    if (preset == ScenePreset::Scene2DualSphereFloorPoint) {
        return "Scene 2: Dual Spheres + Floor + Point Light";
    }
    return "Scene 1: Mary + Floor + Point Light";
}

/**
 * @brief 重置场景容器并初始化相机与环境光。
 */
void ResetSceneState(
    Scene& scene,
    const glm::vec3& cameraPosition,
    const glm::vec3& cameraTarget)
{
    scene.objects.clear();
    scene.lights.clear();
    scene.lightProxyObjectIndex = -1;

    scene.camera = std::make_unique<Camera>();
    scene.camera->setAspectRatio(static_cast<float>(Window::kWindowWidth) / static_cast<float>(Window::kWindowHeight));
    scene.camera->setPosition(cameraPosition);
    scene.camera->setTarget(cameraTarget);

    scene.ambientLightColor = glm::vec3(1.0f);
    scene.ambientLightIntensity = 0.08f;

    Scene::instance = &scene;
}

/**
 * @brief 向场景追加地板网格对象。
 * @return 成功返回 true，失败返回 false。
 */
bool AppendFloorObject(
    Scene& scene,
    const std::string& floorObjPath,
    const glm::vec3& floorPosition,
    const glm::vec3& floorScale)
{
    ObjMeshData floorMesh;
    const bool loadOk = ObjLoader::LoadFromFile(floorObjPath, floorMesh);
    if (!loadOk || floorMesh.empty()) {
        std::cerr << "Failed to load floor.obj: " << floorObjPath << std::endl;
        return false;
    }

    auto floorObject = std::make_unique<MeshObject>(floorMesh);
    floorObject->setScale(floorScale);
    floorObject->setPosition(floorPosition);
    scene.objects.emplace_back(std::move(floorObject));
    return true;
}

/**
 * @brief 向场景追加点光源与其可视化代理球。
 */
void AppendPointLightAndProxy(
    Scene& scene,
    const glm::vec3& lightPosition,
    const glm::vec3& lightDirection,
    float lightIntensity)
{
    auto light = std::make_unique<Light>(
        lightPosition,
        glm::vec3(1.0f, 1.0f, 1.0f),
        lightDirection,
        lightIntensity,
        Light::LightType::Point);

    light->setShadowNearPlane(0.1f);
    light->setShadowFarPlane(12.0f);
    scene.lights.emplace_back(std::move(light));

    auto lightProxySphere = std::make_unique<Sphere>(0.08f, 1, glm::vec3(1.0f, 0.95f, 0.25f));
    lightProxySphere->setPosition(lightPosition);
    lightProxySphere->setCastShadow(false);
    scene.lightProxyObjectIndex = static_cast<int>(scene.objects.size());
    scene.objects.emplace_back(std::move(lightProxySphere));
}

/**
 * @brief 构建场景 1：Mary 模型 + 地板 + 点光源。
 */
void BuildScenePresetOne(Scene& scene, const SceneBuildResources& resources)
{
    ResetSceneState(scene, glm::vec3(0.0f, 0.7f, 3.4f), glm::vec3(0.0f, -0.4f, -2.0f));

    ObjMeshData maryMesh;
    const bool maryLoadOk = ObjLoader::LoadFromFile(resources.maryObjPath, maryMesh);
    if (!maryLoadOk || maryMesh.empty()) {
        std::cerr << "Failed to load mary.obj: " << resources.maryObjPath << std::endl;
        return;
    }

    auto maryObject = std::make_unique<MeshObject>(maryMesh);
    maryObject->setPosition(glm::vec3(0.0f, -1.0f, -2.0f));
    maryObject->setTexture(resources.maryTexture);
    scene.objects.emplace_back(std::move(maryObject));

    (void)AppendFloorObject(
        scene,
        resources.floorObjPath,
        glm::vec3(0.0f, -1.0f, -2.0f),
        glm::vec3(0.08f, 1.0f, 0.08f));

    AppendPointLightAndProxy(
        scene,
        glm::vec3(0.0f, 2.2f, 0.6f),
        glm::vec3(0.0f, -0.62f, -0.78f),
        2.4f);
}

/**
 * @brief 构建场景 2：双球体 + 地板 + 中上方点光源。
 */
void BuildScenePresetTwo(Scene& scene, const SceneBuildResources& resources)
{
    ResetSceneState(scene, glm::vec3(0.0f, 0.9f, 3.8f), glm::vec3(0.0f, -0.2f, -2.0f));

    auto leftSphere = std::make_unique<Sphere>(0.5f, 2, glm::vec3(1.0f, 1.0f, 1.0f));
    leftSphere->setPosition(glm::vec3(-0.85f, -0.45f, -2.0f));
    leftSphere->setTexture(resources.sphereTexture);
    scene.objects.emplace_back(std::move(leftSphere));

    auto rightSphere = std::make_unique<Sphere>(0.5f, 2, glm::vec3(0.95f, 0.95f, 1.0f));
    rightSphere->setPosition(glm::vec3(0.85f, -0.45f, -2.0f));
    rightSphere->setTexture(resources.sphereTexture);
    scene.objects.emplace_back(std::move(rightSphere));

    (void)AppendFloorObject(
        scene,
        resources.floorObjPath,
        glm::vec3(0.0f, -1.0f, -2.0f),
        glm::vec3(0.08f, 1.0f, 0.08f));

    AppendPointLightAndProxy(
        scene,
        glm::vec3(0.0f, 1.75f, -2.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        2.8f);
}

/**
 * @brief 按预设构建场景内容。
 */
void BuildSceneByPreset(Scene& scene, ScenePreset preset, const SceneBuildResources& resources)
{
    if (preset == ScenePreset::Scene2DualSphereFloorPoint) {
        BuildScenePresetTwo(scene, resources);
    } else {
        BuildScenePresetOne(scene, resources);
    }

    // 保证默认阴影分辨率不低于 512，避免切场景后分辨率过低导致阴影阶梯明显。
    scene.shadowSettings.shadowMapResolution = std::max(scene.shadowSettings.shadowMapResolution, 512);
}

/**
 * @brief 把颜色统一转换到 0~1 区间，兼容 0~1 与 0~255 两种输入习惯。
 * @param color 输入颜色，可为线性 0~1 或 8-bit 标准化前的 0~255。
 * @return 返回归一化后的 0~1 颜色。
 */
glm::vec3 NormalizeColorToUnitRange(const glm::vec3& color)
{
    const float maxChannel = std::max(color.r, std::max(color.g, color.b));
    if (maxChannel > 1.0f) {
        return color / 255.0f;
    }
    return color;
}

/**
 * @brief 在世界坐标下执行 Blinn-Phong 光照并写回最终像素。
 * @param colorbuffer 最终颜色缓冲，函数会把结果写入 payload.bufferIndex。
 * @param payload 当前片元输入（颜色、法线、世界坐标、阴影可见性等）。
 * @param scene 场景数据（相机、环境光、光源列表）。
 * @return 无返回值。
 */
void BlingPhongShader(std::vector<std::uint32_t>& colorbuffer, const Fragment& payload, const Scene& scene)
{
    if (!scene.camera) {
        colorbuffer[payload.bufferIndex] = SoftwareRenderer::packColor(payload.color);
        return;
    }

    const glm::vec3 albedo = glm::vec3(payload.color.r, payload.color.g, payload.color.b) / 255.0f;
    const glm::vec3 ambientColor = NormalizeColorToUnitRange(scene.ambientLightColor);

    glm::vec3 normal = payload.normal;
    if (glm::dot(normal, normal) <= 1e-12f) {
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        normal = glm::normalize(normal);
    }

    glm::vec3 viewDir = scene.camera->position() - payload.worldPos;
    if (glm::dot(viewDir, viewDir) <= 1e-12f) {
        viewDir = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        viewDir = glm::normalize(viewDir);
    }

    glm::vec3 ambientLighting = scene.ambientLightIntensity * ambientColor;
    glm::vec3 directDiffuseLighting(0.0f);
    glm::vec3 specularLighting(0.0f);
    glm::vec3 emissiveLighting(0.0f);

    constexpr float kShininess = 64.0f;
    constexpr float kSpecularStrength = 0.35f;

    for (const auto& lightPtr : scene.lights) {
        if (!lightPtr) {
            continue;
        }

        const Light& light = *lightPtr;
        const glm::vec3 lightColor = NormalizeColorToUnitRange(light.color());

        glm::vec3 lightDir(0.0f, 1.0f, 0.0f);
        float attenuation = 1.0f;

        if (light.type() == Light::LightType::Directional) {
            glm::vec3 dir = light.direction();
            if (glm::dot(dir, dir) > 1e-12f) {
                lightDir = -glm::normalize(dir);
            }
        } else {
            const glm::vec3 toLight = light.position() - payload.worldPos;
            const float distanceToLight = std::max(glm::length(toLight), 1e-4f);
            lightDir = toLight / distanceToLight;

            // 使用更平滑的点光衰减，避免 1/r^2 在中距离下光照过暗看不出层次。可按场景尺度调整常数项，当前参数适配本项目默认单位。
            attenuation = 1.0f / (1.0f + 0.14f * distanceToLight + 0.07f * distanceToLight * distanceToLight);

            // 给点光源附近片元增加“灯芯自发光”，让光源可视化球体不会因法线和阴影因素变黑。
            constexpr float kLightCoreRadius = 0.14f;
            if (distanceToLight < kLightCoreRadius) {
                const float coreFactor = 1.0f - distanceToLight / kLightCoreRadius;
                emissiveLighting += lightColor * light.intensity() * coreFactor * 0.65f;
            }
        }

        const float ndotl = std::max(glm::dot(normal, lightDir), 0.0f);
        if (ndotl <= 0.0f) {
            continue;
        }

        const glm::vec3 halfDir = glm::normalize(lightDir + viewDir);
        const float specAngle = std::max(glm::dot(normal, halfDir), 0.0f);
        const float specularTerm = std::pow(specAngle, kShininess);

        directDiffuseLighting += ndotl * light.intensity() * attenuation * lightColor;
        specularLighting += kSpecularStrength * specularTerm * light.intensity() * attenuation * lightColor;
    }

    // 阴影只衰减直射项，环境光与自发光不应被阴影完全压黑。
    const float shadowVisibility = std::clamp(payload.shadowVisibility, 0.0f, 1.0f);
    glm::vec3 finalLinear = albedo * ambientLighting;
    finalLinear += shadowVisibility * (albedo * directDiffuseLighting + specularLighting);
    finalLinear += emissiveLighting;
    finalLinear = glm::clamp(finalLinear, glm::vec3(0.0f), glm::vec3(1.0f));

    Color finalColor;
    finalColor.r = static_cast<std::uint8_t>(finalLinear.r * 255.0f);
    finalColor.g = static_cast<std::uint8_t>(finalLinear.g * 255.0f);
    finalColor.b = static_cast<std::uint8_t>(finalLinear.b * 255.0f);
    finalColor.a = 255;
    colorbuffer[payload.bufferIndex] = SoftwareRenderer::packColor(finalColor);
}

int main(int argc, char* argv[])
{
    const char* argv0 = argc > 0 ? argv[0] : nullptr;
    const std::string sphereTexturePath = ResolveSphereTexturePath(argv0);
    const std::string maryTexturePath = ResolveMaryTexturePath(argv0);
    const std::string maryObjPath = ResolveMaryObjPath(argv0);
    const std::string floorObjPath = ResolveFloorPath(argv0);

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
    SceneBuildResources sceneResources;
    sceneResources.sphereTexture = sphereTexture;
    sceneResources.maryTexture = maryTexture;
    sceneResources.maryObjPath = maryObjPath;
    sceneResources.floorObjPath = floorObjPath;

    ScenePreset currentScenePreset = ScenePreset::Scene1MaryFloorPoint;
    BuildSceneByPreset(scene, currentScenePreset, sceneResources);

    SoftwareRenderer renderer(Window::kWindowWidth, Window::kWindowHeight);
    renderer.setBackfaceCullingEnabled(true);
    renderer.setMsaaSampleCount(1);
    renderer.SetFragmentShader(BlingPhongShader);
    std::cout << "Wireframe overlay: OFF (press F1 to toggle)" << '\n';
    std::cout << "Back-face culling: ON (press F2 to toggle)" << '\n';
    std::cout << "MSAA samples: " << renderer.msaaSampleCount() << "x (press F3 to cycle 1x/2x/4x)" << '\n';
    std::cout << "Debug UI: ON (press F4 to toggle panel)" << '\n';
    std::cout << "Mouse capture: ON (press F5 to toggle for ImGui interaction)" << '\n';

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

    DebugUI debugUI;
    if (!debugUI.initialize(window, presentRenderer)) {
        std::cerr << "DebugUI init failed, continue without ImGui panel" << '\n';
    }
    debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));

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
    bool mouseCaptureEnabled = false;
    bool mouseCaptureSupported = true;

    if (scene.camera) {
        ExtractYawPitchFromCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
        ApplyYawPitchToCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
    }

    SetMouseCaptureState(true, window, mouseCaptureEnabled, mouseCaptureSupported);

    while (running) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const float deltaTime = static_cast<float>(nowCounter - lastFrameCounter) / static_cast<float>(perfFrequency);
        lastFrameCounter = nowCounter;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            debugUI.processEvent(event);

            if (event.type == SDL_QUIT) {
                running = false;
            }

            // 将鼠标相对位移转换为 FPS 相机的 yaw/pitch 变化。保持窗口聚焦并移动鼠标，即可水平转向与上下俯仰。
            if (event.type == SDL_MOUSEMOTION && mouseCaptureEnabled && scene.camera && !debugUI.wantsMouseCapture()) {
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

            // 用 KEYDOWN/KEYUP 维护 WASD 持续按下状态，避免依赖全局键盘状态数组。按下时置 true，抬起时置 false，后续移动逻辑直接读取该状态。
            if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && !debugUI.wantsKeyboardCapture()) {
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

            // 功能键使用 KEYDOWN 的边沿触发（repeat==0），避免连续触发。单击一次 F1/F2/F3/ESC，只会触发一次状态切换或退出。
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                const SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_F4) {
                    debugUI.toggleVisible();
                    std::cout << "Debug UI: "
                              << (debugUI.isVisible() ? "ON" : "OFF")
                              << " (press F4 to toggle panel)" << '\n';
                    continue;
                }

                if (key == SDLK_F5) {
                    SetMouseCaptureState(!mouseCaptureEnabled, window, mouseCaptureEnabled, mouseCaptureSupported);
                    std::cout << "Mouse capture: "
                              << (mouseCaptureEnabled ? "ON" : "OFF")
                              << " (press F5 to toggle for ImGui interaction)" << '\n';
                    continue;
                }

                if (debugUI.wantsKeyboardCapture()) {
                    continue;
                }

                switch (key) {
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

            // 窗口焦点切换时同步鼠标捕获状态，并重置移动状态。失焦时释放鼠标，聚焦时恢复相对模式，避免输入卡键与鼠标漂移。
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    moveForward = false;
                    moveBackward = false;
                    moveLeft = false;
                    moveRight = false;

                    if (mouseCaptureEnabled) {
                        SetMouseCaptureState(false, window, mouseCaptureEnabled, mouseCaptureSupported);
                    }
                }
            }
        }

        if (!running) {
            break;
        }

        // 在主循环安全点处理场景切换请求，避免在 UI 绘制阶段直接重建场景造成生命周期耦合。
        if (debugUI.hasPendingSceneSwitch()) {
            const int requestedPresetIndex = debugUI.consumePendingSceneSwitch();
            const ScenePreset requestedPreset = ScenePresetFromIndex(requestedPresetIndex);
            if (requestedPreset != currentScenePreset) {
                BuildSceneByPreset(scene, requestedPreset, sceneResources);
                currentScenePreset = requestedPreset;
                debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));

                // 切场景后重置连续输入状态，避免沿用旧场景下的按键状态导致相机瞬间漂移。
                moveForward = false;
                moveBackward = false;
                moveLeft = false;
                moveRight = false;

                if (scene.camera) {
                    ExtractYawPitchFromCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
                    ApplyYawPitchToCamera(*scene.camera, cameraYawDeg, cameraPitchDeg);
                }

                std::cout << "Scene switched to: " << ScenePresetName(currentScenePreset) << '\n';
            } else {
                debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));
            }
        }

        if (debugUI.wantsKeyboardCapture()) {
            moveForward = false;
            moveBackward = false;
            moveLeft = false;
            moveRight = false;
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

        // 每帧同步光源代理球位置，确保相机看到的光源标记与真实点光位置一致。当 UI 拖动点光源位置时，代理球会在同一帧跟随更新。
        if (!scene.lights.empty()
            && scene.lights[0]
            && scene.lightProxyObjectIndex >= 0
            && scene.lightProxyObjectIndex < static_cast<int>(scene.objects.size())
            && scene.objects[static_cast<std::size_t>(scene.lightProxyObjectIndex)]) {
            scene.objects[static_cast<std::size_t>(scene.lightProxyObjectIndex)]->setPosition(scene.lights[0]->position());
        }

        scene.RotateObjects(deltaTime);

        renderer.clear({ 0, 0, 0, 255 });
        renderer.DrawScene(scene);

        debugUI.beginFrame();
        debugUI.drawShadowPanel(scene, renderer);

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
        debugUI.render();
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

    debugUI.shutdown();
    SDL_DestroyTexture(framebufferTexture);
    SDL_DestroyRenderer(presentRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
