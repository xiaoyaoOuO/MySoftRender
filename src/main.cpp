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

// 解析天空盒根目录路径（assets/cubemap）。程序启动时调用，用于扫描所有可用天空盒目录。
std::string ResolveCubemapRootPath(const char* argv0)
{
    return ResolveAssetPath(argv0, std::filesystem::path("assets") / "cubemap");
}

/**
 * @brief 记录单个天空盒资源目录及其已加载纹理对象。
 */
struct SkyboxAssetEntry
{
    std::string name;
    std::string directoryPath;
    std::shared_ptr<CubemapTexture> texture;
};

/**
 * @brief 扫描天空盒根目录并加载所有可用的立方体贴图。
 * @param cubemapRootPath 天空盒根目录（通常为 assets/cubemap）。
 * @return 返回可用天空盒列表（已按目录名排序）。
 */
std::vector<SkyboxAssetEntry> DiscoverSkyboxAssets(const std::string& cubemapRootPath)
{
    namespace fs = std::filesystem;

    std::vector<SkyboxAssetEntry> assets;
    std::error_code ec;
    const fs::path rootPath(cubemapRootPath);
    if (!fs::exists(rootPath, ec) || ec || !fs::is_directory(rootPath, ec) || ec) {
        return assets;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(rootPath, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory(ec) || ec) {
            ec.clear();
            continue;
        }

        auto cubemapTexture = std::make_shared<CubemapTexture>();
        if (!cubemapTexture->loadFromDirectory(entry.path().string())) {
            continue;
        }

        SkyboxAssetEntry asset;
        asset.name = entry.path().filename().string();
        asset.directoryPath = entry.path().string();
        asset.texture = std::move(cubemapTexture);
        assets.emplace_back(std::move(asset));
    }

    std::sort(
        assets.begin(),
        assets.end(),
        [](const SkyboxAssetEntry& lhs, const SkyboxAssetEntry& rhs) {
            return lhs.name < rhs.name;
        });
    return assets;
}

/**
 * @brief 将天空盒资源列表转换为 UI 下拉框文本列表。
 * @param assets 已加载天空盒资源列表。
 * @return 返回与 assets 同顺序的名称数组。
 */
std::vector<std::string> BuildSkyboxNames(const std::vector<SkyboxAssetEntry>& assets)
{
    std::vector<std::string> names;
    names.reserve(assets.size());
    for (const SkyboxAssetEntry& asset : assets) {
        names.push_back(asset.name);
    }
    return names;
}

/**
 * @brief 按索引把天空盒应用到场景。
 * @param scene 当前场景对象。
 * @param assets 可用天空盒资源列表。
 * @param skyboxIndex 目标天空盒索引。
 * @return 索引合法且应用成功时返回 true。
 */
bool ApplySkyboxByIndex(Scene& scene, const std::vector<SkyboxAssetEntry>& assets, int skyboxIndex)
{
    if (skyboxIndex < 0 || skyboxIndex >= static_cast<int>(assets.size())) {
        return false;
    }

    scene.skyboxTexture = assets[static_cast<std::size_t>(skyboxIndex)].texture;
    scene.skyboxName = assets[static_cast<std::size_t>(skyboxIndex)].name;
    scene.enableSkybox = static_cast<bool>(scene.skyboxTexture);
    return scene.enableSkybox;
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

/**
 * @brief 运行时输入状态，集中管理主循环涉及的相机与键盘输入标志。
 */
struct InputRuntimeState
{
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    float cameraYawDeg = 0.0f;
    float cameraPitchDeg = 0.0f;
    bool mouseCaptureEnabled = false;
    bool mouseCaptureSupported = true;
};

/**
 * @brief 运行时帧统计状态，用于 deltaTime 与 FPS 标题刷新。
 */
struct FrameRuntimeState
{
    Uint64 perfFrequency = 0;
    Uint64 fpsWindowStartCounter = 0;
    Uint64 lastFrameCounter = 0;
    int fpsFrameCount = 0;
};

constexpr float kMouseSensitivityDegPerPixel = 0.12f;
constexpr float kMaxPitchDeg = 89.0f;

/**
 * @brief 重置 WASD 连续移动状态，避免跨事件/场景残留输入。
 */
void ResetMovementInputState(InputRuntimeState& inputState)
{
    inputState.moveForward = false;
    inputState.moveBackward = false;
    inputState.moveLeft = false;
    inputState.moveRight = false;
}

/**
 * @brief 加载球体贴图，失败时自动回退为棋盘格纹理。
 */
std::shared_ptr<Texture2D> LoadSphereTextureWithFallback(const std::string& sphereTexturePath)
{
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
    return sphereTexture;
}

/**
 * @brief 加载 Mary 贴图，失败时返回空指针并继续使用顶点色渲染。
 */
std::shared_ptr<Texture2D> LoadMaryTextureOptional(const std::string& maryTexturePath)
{
    std::shared_ptr<Texture2D> maryTexture = std::make_shared<Texture2D>();
    if (!maryTexture->loadFromFile(maryTexturePath)) {
        maryTexture.reset();
        std::cout << "Mary texture: not found, rendering with vertex color" << '\n';
    } else {
        std::cout << "Mary texture loaded from: " << maryTexturePath << '\n';
    }
    return maryTexture;
}

/**
 * @brief 初始化并应用默认天空盒，返回当前生效的天空盒索引。
 */
int InitializeSkyboxSelection(
    Scene& scene,
    const std::vector<SkyboxAssetEntry>& skyboxAssets,
    const std::string& cubemapRootPath)
{
    if (!skyboxAssets.empty()) {
        int currentSkyboxIndex = 0;
        if (ApplySkyboxByIndex(scene, skyboxAssets, currentSkyboxIndex)) {
            std::cout << "Skybox loaded: " << scene.skyboxName
                      << " (" << skyboxAssets[static_cast<std::size_t>(currentSkyboxIndex)].directoryPath << ")" << '\n';
        }
        std::cout << "Skybox assets discovered: " << skyboxAssets.size() << '\n';
        return currentSkyboxIndex;
    }

    scene.skyboxTexture.reset();
    scene.skyboxName = "None";
    scene.enableSkybox = false;
    std::cout << "Skybox: no valid cubemap found under: " << cubemapRootPath << '\n';
    return -1;
}

// 初始化渲染器默认开关与默认着色器。
void ConfigureRendererDefaults(SoftwareRenderer& renderer)
{
    renderer.setBackfaceCullingEnabled(true);
    renderer.setMsaaSampleCount(1);
    renderer.SetFragmentShader(BlingPhongShader);

    std::cout << "Wireframe overlay: OFF (press F1 to toggle)" << '\n';
    std::cout << "Back-face culling: ON (press F2 to toggle)" << '\n';
    std::cout << "MSAA samples: " << renderer.msaaSampleCount() << "x (press F3 to cycle 1x/2x/4x)" << '\n';
    std::cout << "Debug UI: ON (press F4 to toggle panel)" << '\n';
    std::cout << "Mouse capture: ON (press F5 to toggle for ImGui interaction)" << '\n';
}

// 创建 SDL 窗口、呈现器与帧缓冲纹理。
bool CreateSdlPresentationResources(
    SDL_Window*& window,
    SDL_Renderer*& presentRenderer,
    SDL_Texture*& framebufferTexture)
{
    window = nullptr;
    presentRenderer = nullptr;
    framebufferTexture = nullptr;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    window = SDL_CreateWindow(
        "mySoftRender",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        Window::kWindowWidth,
        Window::kWindowHeight,
        SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return false;
    }

    const Uint32 rendererFlags = SDL_RENDERER_ACCELERATED
        | (Window::kEnablePresentVSync ? SDL_RENDERER_PRESENTVSYNC : 0u);
    presentRenderer = SDL_CreateRenderer(window, -1, rendererFlags);
    if (presentRenderer == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return false;
    }

    framebufferTexture = SDL_CreateTexture(
        presentRenderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        Window::kWindowWidth,
        Window::kWindowHeight);

    if (framebufferTexture == nullptr) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(presentRenderer);
        presentRenderer = nullptr;
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return false;
    }

    return true;
}

// 释放 SDL 呈现相关资源并关闭 SDL 子系统。
void DestroySdlPresentationResources(
    SDL_Window* window,
    SDL_Renderer* presentRenderer,
    SDL_Texture* framebufferTexture)
{
    if (framebufferTexture != nullptr) {
        SDL_DestroyTexture(framebufferTexture);
    }
    if (presentRenderer != nullptr) {
        SDL_DestroyRenderer(presentRenderer);
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

/**
 * @brief 初始化每帧统计状态。
 */
FrameRuntimeState InitializeFrameRuntimeState()
{
    FrameRuntimeState frameState;
    frameState.perfFrequency = SDL_GetPerformanceFrequency();
    frameState.fpsWindowStartCounter = SDL_GetPerformanceCounter();
    frameState.lastFrameCounter = frameState.fpsWindowStartCounter;
    frameState.fpsFrameCount = 0;
    return frameState;
}

/**
 * @brief 计算当前帧 deltaTime，并推进帧计时游标。
 */
float BeginFrameTiming(FrameRuntimeState& frameState)
{
    const Uint64 nowCounter = SDL_GetPerformanceCounter();
    const float deltaTime = static_cast<float>(nowCounter - frameState.lastFrameCounter)
        / static_cast<float>(frameState.perfFrequency);
    frameState.lastFrameCounter = nowCounter;
    return deltaTime;
}

/**
 * @brief 根据统计窗口刷新 FPS 到窗口标题。
 */
void UpdateWindowTitleWithFps(SDL_Window* window, FrameRuntimeState& frameState)
{
    ++frameState.fpsFrameCount;
    const Uint64 fpsCounter = SDL_GetPerformanceCounter();
    const double elapsedSeconds = static_cast<double>(fpsCounter - frameState.fpsWindowStartCounter)
        / static_cast<double>(frameState.perfFrequency);
    if (elapsedSeconds >= 0.5) {
        const double fps = static_cast<double>(frameState.fpsFrameCount) / elapsedSeconds;
        char title[128];
        std::snprintf(title, sizeof(title), "mySoftRender - FPS: %.1f", fps);
        SDL_SetWindowTitle(window, title);

        frameState.fpsFrameCount = 0;
        frameState.fpsWindowStartCounter = fpsCounter;
    }
}

/**
 * @brief 用当前相机方向初始化鼠标视角控制，并默认开启鼠标捕获。
 */
void InitializeCameraControlState(Scene& scene, SDL_Window* window, InputRuntimeState& inputState)
{
    if (scene.camera) {
        ExtractYawPitchFromCamera(*scene.camera, inputState.cameraYawDeg, inputState.cameraPitchDeg);
        ApplyYawPitchToCamera(*scene.camera, inputState.cameraYawDeg, inputState.cameraPitchDeg);
    }

    SetMouseCaptureState(
        true,
        window,
        inputState.mouseCaptureEnabled,
        inputState.mouseCaptureSupported);
}

/**
 * @brief 处理鼠标相对移动事件，更新 FPS 相机的 yaw/pitch。
 */
void HandleMouseMotionEvent(
    const SDL_Event& event,
    Scene& scene,
    const DebugUI& debugUI,
    InputRuntimeState& inputState)
{
    if (event.type != SDL_MOUSEMOTION
        || !inputState.mouseCaptureEnabled
        || !scene.camera
        || debugUI.wantsMouseCapture()) {
        return;
    }

    inputState.cameraYawDeg += static_cast<float>(event.motion.xrel) * kMouseSensitivityDegPerPixel;
    inputState.cameraPitchDeg -= static_cast<float>(event.motion.yrel) * kMouseSensitivityDegPerPixel;

    if (inputState.cameraPitchDeg > kMaxPitchDeg) {
        inputState.cameraPitchDeg = kMaxPitchDeg;
    }
    if (inputState.cameraPitchDeg < -kMaxPitchDeg) {
        inputState.cameraPitchDeg = -kMaxPitchDeg;
    }

    ApplyYawPitchToCamera(*scene.camera, inputState.cameraYawDeg, inputState.cameraPitchDeg);
}

/**
 * @brief 处理 KEYDOWN/KEYUP 对应的 WASD 连续移动状态。
 */
void HandleMovementKeyEvent(
    const SDL_Event& event,
    const DebugUI& debugUI,
    InputRuntimeState& inputState)
{
    if ((event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) || debugUI.wantsKeyboardCapture()) {
        return;
    }

    const bool isKeyDown = (event.type == SDL_KEYDOWN);
    const SDL_Keycode sym = event.key.keysym.sym;
    switch (sym) {
    case SDLK_w:
        inputState.moveForward = isKeyDown;
        break;
    case SDLK_s:
        inputState.moveBackward = isKeyDown;
        break;
    case SDLK_a:
        inputState.moveLeft = isKeyDown;
        break;
    case SDLK_d:
        inputState.moveRight = isKeyDown;
        break;
    default:
        break;
    }
}

/**
 * @brief 处理功能键（F1~F5、ESC）对应的运行时开关与退出逻辑。
 */
void HandleFunctionKeyEvent(
    const SDL_Event& event,
    bool& running,
    SoftwareRenderer& renderer,
    DebugUI& debugUI,
    SDL_Window* window,
    InputRuntimeState& inputState)
{
    if (event.type != SDL_KEYDOWN || event.key.repeat != 0) {
        return;
    }

    const SDL_Keycode key = event.key.keysym.sym;

    if (key == SDLK_F4) {
        debugUI.toggleVisible();
        std::cout << "Debug UI: "
                  << (debugUI.isVisible() ? "ON" : "OFF")
                  << " (press F4 to toggle panel)" << '\n';
        return;
    }

    if (key == SDLK_F5) {
        SetMouseCaptureState(
            !inputState.mouseCaptureEnabled,
            window,
            inputState.mouseCaptureEnabled,
            inputState.mouseCaptureSupported);
        std::cout << "Mouse capture: "
                  << (inputState.mouseCaptureEnabled ? "ON" : "OFF")
                  << " (press F5 to toggle for ImGui interaction)" << '\n';
        return;
    }

    if (debugUI.wantsKeyboardCapture()) {
        return;
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

/**
 * @brief 处理窗口焦点事件，失焦时释放鼠标并清空移动输入。
 */
void HandleWindowFocusEvent(const SDL_Event& event, SDL_Window* window, InputRuntimeState& inputState)
{
    if (event.type != SDL_WINDOWEVENT || event.window.event != SDL_WINDOWEVENT_FOCUS_LOST) {
        return;
    }

    ResetMovementInputState(inputState);
    if (inputState.mouseCaptureEnabled) {
        SetMouseCaptureState(
            false,
            window,
            inputState.mouseCaptureEnabled,
            inputState.mouseCaptureSupported);
    }
}

/**
 * @brief 轮询并处理本帧所有 SDL 事件。
 */
void PollAndProcessEvents(
    bool& running,
    Scene& scene,
    SoftwareRenderer& renderer,
    DebugUI& debugUI,
    SDL_Window* window,
    InputRuntimeState& inputState)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        debugUI.processEvent(event);

        if (event.type == SDL_QUIT) {
            running = false;
        }

        HandleMouseMotionEvent(event, scene, debugUI, inputState);
        HandleMovementKeyEvent(event, debugUI, inputState);
        HandleFunctionKeyEvent(event, running, renderer, debugUI, window, inputState);
        HandleWindowFocusEvent(event, window, inputState);
    }
}

/**
 * @brief 在主循环安全点处理场景切换，避免 UI 回调中直接重建场景。
 */
void ProcessPendingSceneSwitch(
    Scene& scene,
    DebugUI& debugUI,
    const SceneBuildResources& sceneResources,
    const std::vector<SkyboxAssetEntry>& skyboxAssets,
    ScenePreset& currentScenePreset,
    int& currentSkyboxIndex,
    InputRuntimeState& inputState)
{
    if (!debugUI.hasPendingSceneSwitch()) {
        return;
    }

    const int requestedPresetIndex = debugUI.consumePendingSceneSwitch();
    const ScenePreset requestedPreset = ScenePresetFromIndex(requestedPresetIndex);
    if (requestedPreset != currentScenePreset) {
        BuildSceneByPreset(scene, requestedPreset, sceneResources);
        if (currentSkyboxIndex >= 0) {
            (void)ApplySkyboxByIndex(scene, skyboxAssets, currentSkyboxIndex);
        }
        currentScenePreset = requestedPreset;
        debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));

        ResetMovementInputState(inputState);
        if (scene.camera) {
            ExtractYawPitchFromCamera(*scene.camera, inputState.cameraYawDeg, inputState.cameraPitchDeg);
            ApplyYawPitchToCamera(*scene.camera, inputState.cameraYawDeg, inputState.cameraPitchDeg);
        }

        std::cout << "Scene switched to: " << ScenePresetName(currentScenePreset) << '\n';
    } else {
        debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));
    }
}

/**
 * @brief 在主循环安全点处理天空盒切换请求。
 */
void ProcessPendingSkyboxSwitch(
    Scene& scene,
    DebugUI& debugUI,
    const std::vector<SkyboxAssetEntry>& skyboxAssets,
    int& currentSkyboxIndex)
{
    if (!debugUI.hasPendingSkyboxSwitch()) {
        return;
    }

    const int requestedSkyboxIndex = debugUI.consumePendingSkyboxSwitch();
    const bool indexValid = requestedSkyboxIndex >= 0
        && requestedSkyboxIndex < static_cast<int>(skyboxAssets.size());

    if (indexValid && requestedSkyboxIndex != currentSkyboxIndex) {
        if (ApplySkyboxByIndex(scene, skyboxAssets, requestedSkyboxIndex)) {
            currentSkyboxIndex = requestedSkyboxIndex;
            std::cout << "Skybox switched to: " << scene.skyboxName << '\n';
        }
    }
    debugUI.setCurrentSkyboxIndex(currentSkyboxIndex);
}

/**
 * @brief 执行每帧的输入驱动更新与场景状态同步。
 */
void UpdateSceneForFrame(
    Scene& scene,
    const DebugUI& debugUI,
    InputRuntimeState& inputState,
    float deltaTime)
{
    if (debugUI.wantsKeyboardCapture()) {
        ResetMovementInputState(inputState);
    }

    if (scene.camera) {
        MoveCameraWithInput(
            *scene.camera,
            inputState.moveForward,
            inputState.moveBackward,
            inputState.moveLeft,
            inputState.moveRight,
            deltaTime);
    }

    if (!scene.lights.empty() && scene.lights[0]
        && scene.lightProxyObjectIndex >= 0
        && scene.lightProxyObjectIndex < static_cast<int>(scene.objects.size())
        && scene.objects[static_cast<std::size_t>(scene.lightProxyObjectIndex)]) {
        scene.objects[static_cast<std::size_t>(scene.lightProxyObjectIndex)]->setPosition(scene.lights[0]->position());
    }

    scene.RotateObjects(deltaTime);
}

// 执行一帧渲染与 UI 呈现。
bool RenderAndPresentFrame(
    Scene& scene,
    SoftwareRenderer& renderer,
    DebugUI& debugUI,
    SDL_Renderer* presentRenderer,
    SDL_Texture* framebufferTexture)
{
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
        return false;
    }

    SDL_RenderClear(presentRenderer);
    SDL_RenderCopy(presentRenderer, framebufferTexture, nullptr, nullptr);
    debugUI.render();
    SDL_RenderPresent(presentRenderer);
    return true;
}

int main(int argc, char* argv[])
{
    const char* argv0 = argc > 0 ? argv[0] : nullptr;

    // 解析资源路径并加载必要的纹理与模型资源。
    const std::string sphereTexturePath = ResolveSphereTexturePath(argv0);
    const std::string maryTexturePath = ResolveMaryTexturePath(argv0);
    const std::string maryObjPath = ResolveMaryObjPath(argv0);
    const std::string floorObjPath = ResolveFloorPath(argv0);
    const std::string cubemapRootPath = ResolveCubemapRootPath(argv0);

    std::shared_ptr<Texture2D> sphereTexture = LoadSphereTextureWithFallback(sphereTexturePath);
    std::shared_ptr<Texture2D> maryTexture = LoadMaryTextureOptional(maryTexturePath);

    Scene scene;
    SceneBuildResources sceneResources;
    sceneResources.sphereTexture = sphereTexture;
    sceneResources.maryTexture = maryTexture;
    sceneResources.maryObjPath = maryObjPath;
    sceneResources.floorObjPath = floorObjPath;

    ScenePreset currentScenePreset = ScenePreset::Scene1MaryFloorPoint;
    BuildSceneByPreset(scene, currentScenePreset, sceneResources);

    const std::vector<SkyboxAssetEntry> skyboxAssets = DiscoverSkyboxAssets(cubemapRootPath);
    const std::vector<std::string> skyboxNames = BuildSkyboxNames(skyboxAssets);
    int currentSkyboxIndex = InitializeSkyboxSelection(scene, skyboxAssets, cubemapRootPath);

    SoftwareRenderer renderer(Window::kWindowWidth, Window::kWindowHeight);
    ConfigureRendererDefaults(renderer);

    SDL_Window* window = nullptr;
    SDL_Renderer* presentRenderer = nullptr;
    SDL_Texture* framebufferTexture = nullptr;
    if (!CreateSdlPresentationResources(window, presentRenderer, framebufferTexture)) {
        return 1;
    }

    DebugUI debugUI;
    if (!debugUI.initialize(window, presentRenderer)) {
        std::cerr << "DebugUI init failed, continue without ImGui panel" << '\n';
    }
    debugUI.setCurrentScenePreset(ScenePresetToIndex(currentScenePreset));
    debugUI.setSkyboxOptions(skyboxNames);
    debugUI.setCurrentSkyboxIndex(currentSkyboxIndex);

    bool running = true;
    FrameRuntimeState frameState = InitializeFrameRuntimeState();
    InputRuntimeState inputState;
    InitializeCameraControlState(scene, window, inputState);

    while (running) {
        const float deltaTime = BeginFrameTiming(frameState);
        PollAndProcessEvents(running, scene, renderer, debugUI, window, inputState);

        if (!running) {
            break;
        }

        ProcessPendingSceneSwitch(scene,debugUI,sceneResources,skyboxAssets,currentScenePreset,currentSkyboxIndex,inputState);

        ProcessPendingSkyboxSwitch(scene, debugUI, skyboxAssets, currentSkyboxIndex);

        UpdateSceneForFrame(scene, debugUI, inputState, deltaTime);

        if (!RenderAndPresentFrame(scene, renderer, debugUI, presentRenderer, framebufferTexture)) {
            break;
        }

        UpdateWindowTitleWithFps(window, frameState);
    }

    debugUI.shutdown();
    DestroySdlPresentationResources(window, presentRenderer, framebufferTexture);

    return 0;
}
