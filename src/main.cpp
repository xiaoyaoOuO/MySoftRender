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
#include <array>

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
    std::shared_ptr<CubemapTexture> irradianceTexture;
    std::array<std::shared_ptr<CubemapTexture>, 6> specularLodTextures;
    std::array<int, 6> specularLodFallbackSources = {{-1, -1, -1, -1, -1, -1}};
    std::string specularLodRootPath;
    std::shared_ptr<CubemapTexture> prefilterTexture;
    std::shared_ptr<Texture2D> brdfLutTexture;
};

/**
 * @brief Specular 六级 LOD 贴图加载结果。
 */
struct SpecularLodLoadResult
{
    std::array<std::shared_ptr<CubemapTexture>, 6> textures;
    std::array<int, 6> fallbackSources = {{-1, -1, -1, -1, -1, -1}};
    std::string rootPath;
};

/**
 * @brief 尝试从目录加载可选立方体贴图资源，失败时返回空指针。
 */
std::shared_ptr<CubemapTexture> LoadOptionalCubemap(const std::filesystem::path& directoryPath)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(directoryPath, ec) || ec || !fs::is_directory(directoryPath, ec) || ec) {
        return nullptr;
    }

    auto cubemap = std::make_shared<CubemapTexture>();
    if (!cubemap->loadFromDirectory(directoryPath.string())) {
        return nullptr;
    }
    return cubemap;
}

/**
 * @brief 从已加载的 Specular LOD 列表中，为目标档位查找最近可用档。
 * @return 返回最近可用源档位；若全部缺失返回 -1。
 */
int FindNearestSpecularLodSource(
    const std::array<std::shared_ptr<CubemapTexture>, 6>& directLoaded,
    int requestedLod)
{
    int bestSource = -1;
    int bestDistance = 1000;

    for (int sourceLod = 0; sourceLod < static_cast<int>(directLoaded.size()); ++sourceLod) {
        if (!directLoaded[static_cast<std::size_t>(sourceLod)]
            || !directLoaded[static_cast<std::size_t>(sourceLod)]->valid()) {
            continue;
        }

        const int distance = std::abs(sourceLod - requestedLod);
        if (distance < bestDistance || (distance == bestDistance && sourceLod < bestSource)) {
            bestDistance = distance;
            bestSource = sourceLod;
        }
    }

    return bestSource;
}

/**
 * @brief 加载单个天空盒的 Specular 六级 LOD 贴图，并为缺档建立最近回退映射。
 */
SpecularLodLoadResult LoadSpecularLodCubemaps(const std::filesystem::path& skyboxPath)
{
    namespace fs = std::filesystem;

    SpecularLodLoadResult result;

    const std::string skyboxName = skyboxPath.filename().string();
    const fs::path iblRootPath = skyboxPath / "ibl";
    const fs::path covRootPath = skyboxPath / (skyboxName + "_cov");
    const fs::path specLodRootPath = iblRootPath / "specular_lod";
    const fs::path prefilterLodRootPath = iblRootPath / "prefilter_lod";

    // 当前约定：_cov 卷积资源用于 Specular 高光采样，缺失时再回退到其它 specular 目录。
    const std::array<fs::path, 3> candidateRoots = {
        covRootPath,
        specLodRootPath,
        prefilterLodRootPath
    };

    std::array<std::shared_ptr<CubemapTexture>, 6> directLoaded;
    std::array<bool, 3> usedRoots = {{false, false, false}};

    for (int lod = 0; lod < static_cast<int>(directLoaded.size()); ++lod) {
        const fs::path lodPathName = "lod" + std::to_string(lod);

        for (int rootIndex = 0; rootIndex < static_cast<int>(candidateRoots.size()); ++rootIndex) {
            auto lodMap = LoadOptionalCubemap(candidateRoots[static_cast<std::size_t>(rootIndex)] / lodPathName);
            if (!lodMap) {
                continue;
            }

            directLoaded[static_cast<std::size_t>(lod)] = std::move(lodMap);
            usedRoots[static_cast<std::size_t>(rootIndex)] = true;
            break;
        }
    }

    std::vector<std::string> usedRootPaths;
    for (int rootIndex = 0; rootIndex < static_cast<int>(candidateRoots.size()); ++rootIndex) {
        if (usedRoots[static_cast<std::size_t>(rootIndex)]) {
            usedRootPaths.emplace_back(candidateRoots[static_cast<std::size_t>(rootIndex)].string());
        }
    }

    if (!usedRootPaths.empty()) {
        result.rootPath = usedRootPaths[0];
        if (usedRootPaths.size() > 1) {
            result.rootPath += " (mixed with ";
            for (std::size_t i = 1; i < usedRootPaths.size(); ++i) {
                if (i > 1) {
                    result.rootPath += ", ";
                }
                result.rootPath += usedRootPaths[i];
            }
            result.rootPath += ")";
        }
    }

    for (int lod = 0; lod < static_cast<int>(result.textures.size()); ++lod) {
        int sourceLod = -1;
        if (directLoaded[static_cast<std::size_t>(lod)]
            && directLoaded[static_cast<std::size_t>(lod)]->valid()) {
            sourceLod = lod;
        } else {
            sourceLod = FindNearestSpecularLodSource(directLoaded, lod);
        }

        result.fallbackSources[static_cast<std::size_t>(lod)] = sourceLod;
        if (sourceLod >= 0) {
            result.textures[static_cast<std::size_t>(lod)] = directLoaded[static_cast<std::size_t>(sourceLod)];
        }
    }

    return result;
}

/**
 * @brief 尝试加载 BRDF LUT 纹理，按常见命名依次回退。
 */
std::shared_ptr<Texture2D> LoadOptionalBrdfLut(const std::filesystem::path& iblRootPath)
{
    namespace fs = std::filesystem;

    const std::string skyboxName = iblRootPath.parent_path().filename().string();
    std::vector<std::string> candidateNames;
    candidateNames.reserve(12);

    auto appendCandidateName = [&candidateNames](const std::string& name) {
        if (name.empty()) {
            return;
        }
        if (std::find(candidateNames.begin(), candidateNames.end(), name) == candidateNames.end()) {
            candidateNames.push_back(name);
        }
    };

    // 优先匹配“每个 cubemap 自己的 LUT 命名”，再回退到通用命名。
    appendCandidateName(skyboxName + "_lut.png");
    appendCandidateName(skyboxName + "_lut.ppm");
    appendCandidateName(skyboxName + "_brdf_lut.png");
    appendCandidateName(skyboxName + "_brdf_lut.ppm");

    appendCandidateName("skybox_lut.png");
    appendCandidateName("skybox_lut.ppm");
    appendCandidateName("brdf_lut.png");
    appendCandidateName("brdf_lut.ppm");
    appendCandidateName("brdf_lut.jpg");
    appendCandidateName("brdf_lut.jpeg");
    appendCandidateName("brdfLUT.png");
    appendCandidateName("brdf.png");

    for (const std::string& fileName : candidateNames) {
        const fs::path filePath = iblRootPath / fileName;
        std::error_code ec;
        if (!fs::exists(filePath, ec) || ec || !fs::is_regular_file(filePath, ec) || ec) {
            continue;
        }

        auto texture = std::make_shared<Texture2D>();
        if (texture->loadFromFile(filePath.string(), false)) {
            return texture;
        }
    }

    return nullptr;
}

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
        const fs::path iblRootPath = entry.path() / "ibl";
        asset.irradianceTexture = LoadOptionalCubemap(iblRootPath / "irradiance");
        const SpecularLodLoadResult specularLodResult = LoadSpecularLodCubemaps(entry.path());
        asset.specularLodTextures = specularLodResult.textures;
        asset.specularLodFallbackSources = specularLodResult.fallbackSources;
        asset.specularLodRootPath = specularLodResult.rootPath;
        asset.prefilterTexture = LoadOptionalCubemap(iblRootPath / "prefilter");
        asset.brdfLutTexture = LoadOptionalBrdfLut(iblRootPath);
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

    const SkyboxAssetEntry& selectedAsset = assets[static_cast<std::size_t>(skyboxIndex)];
    scene.skyboxTexture = selectedAsset.texture;
    scene.skyboxName = selectedAsset.name;
    scene.iblIrradianceMap = selectedAsset.irradianceTexture;
    scene.IBLSpecLodMaps = selectedAsset.specularLodTextures;
    scene.IBLSpecLodFallbackSources = selectedAsset.specularLodFallbackSources;
    scene.iblSpecLodRootPath = selectedAsset.specularLodRootPath;
    scene.iblPrefilterMap = selectedAsset.prefilterTexture;
    scene.iblBrdfLut = selectedAsset.brdfLutTexture;
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
        return "Scene 2: Sphere (No Texture/No Light) + Skybox";
    }
    return "Scene 1: Mary + Floor + Point Light (Skybox Off)";
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
    scene.iblSettings.enableIBL = true;
    scene.iblSettings.enableDiffuseIBL = true;
    scene.iblSettings.enableSpecularIBL = false;
    scene.iblSettings.diffuseIntensity = 1.0f;
    scene.iblSettings.specularIntensity = 1.0f;
    scene.iblSettings.specularLodMode = SpecularLodMode::Auto;
    scene.iblSettings.specularManualLod = 2;

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
    scene.enableSkybox = false;
    scene.iblSettings.enableIBL = false;
    scene.iblSettings.enableDiffuseIBL = false;
    scene.iblSettings.enableSpecularIBL = false;

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
 * @brief 构建场景 2：无贴图球体 + 无光源 + 天空盒。
 */
void BuildScenePresetTwo(Scene& scene, const SceneBuildResources& resources)
{
    (void)resources;
    ResetSceneState(scene, glm::vec3(0.0f, 0.65f, 3.2f), glm::vec3(0.0f, -0.15f, -2.0f));
    scene.iblSettings.enableIBL = true;
    scene.iblSettings.enableDiffuseIBL = true;
    scene.iblSettings.enableSpecularIBL = true;

    // 直接用 shared_ptr 创建材质，避免手动 new 造成泄漏风险。
    auto metalMaterial = std::make_shared<Material>();
    metalMaterial->metallic = 1.0f;
    metalMaterial->roughness = 0.2f;
    metalMaterial->albedo = glm::vec3(0.9f, 0.9f, 0.9f);

    auto sphere = std::make_unique<Sphere>(0.65f, 2, glm::vec3(0.9f, 0.9f, 0.9f));
    sphere->setPosition(glm::vec3(0.0f, -0.35f, -2.0f));
    sphere->setMaterial(metalMaterial);
    scene.objects.emplace_back(std::move(sphere));

    scene.enableSkybox = true;
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
 * @brief 安全归一化方向向量，零向量时返回指定回退方向。
 */
glm::vec3 NormalizeDirectionSafe(const glm::vec3& dir, const glm::vec3& fallbackDir)
{
    if (glm::dot(dir, dir) <= 1e-12f) {
        return fallbackDir;
    }
    return glm::normalize(dir);
}

//避免拷贝，直接修改传入dir
void NormalizeDirectionSafeInPlace(glm::vec3& dir, const glm::vec3& fallbackDir)
{
    if (glm::dot(dir, dir) <= 1e-12f) {
        dir = fallbackDir;
    } else {
        dir = glm::normalize(dir);
    }
}

/**
 * @brief 计算常量环境光（原有 ambientColor * ambientIntensity 逻辑）。
 * @return 返回常量环境光线性颜色。
 */
glm::vec3 ComputeConstantAmbientLighting(const Scene& scene)
{
    const glm::vec3 ambientColor = NormalizeColorToUnitRange(scene.ambientLightColor);
    return std::max(scene.ambientLightIntensity, 0.0f) * ambientColor;
}

/**
 * @brief 按 roughness 自动映射 Specular LOD（0~5）。
 */
float ComputeSpecularLodLevel(float roughness)
{
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);
    return safeRoughness * 5.0f;
}

/**
 * @brief 按 roughness 自动映射 Specular LOD 档位索引（0~5）。
 */
int ComputeSpecularLod(float roughness)
{
    return std::clamp(static_cast<int>(std::round(ComputeSpecularLodLevel(roughness))), 0, 5);
}

/**
 * @brief 根据场景配置选择本次 Specular IBL 的目标 LOD 档位。
 */
int SelectSpecularLod(const Scene& scene, float roughness)
{
    if (scene.iblSettings.specularLodMode == SpecularLodMode::Manual) {
        return std::clamp(scene.iblSettings.specularManualLod, 0, 5);
    }
    return ComputeSpecularLod(roughness);
}

/**
 * @brief 计算 Diffuse IBL 环境光分量。（Spherical Harmonics后续）
 * @param worldNormal 片元世界法线（默认按法线方向采样环境图）。
 */
glm::vec3 ComputeDiffuseIblLighting(const Scene& scene, const glm::vec3& worldNormal)
{
    if (!scene.iblSettings.enableIBL || !scene.iblSettings.enableDiffuseIBL) {
        return glm::vec3(0.0f);
    }

    const CubemapTexture* iblSource = nullptr;
    const bool hasIrradianceMap = scene.iblIrradianceMap && scene.iblIrradianceMap->valid();
    if (hasIrradianceMap) {
        iblSource = scene.iblIrradianceMap.get();
    } else if (scene.skyboxTexture && scene.skyboxTexture->valid()) {
        iblSource = scene.skyboxTexture.get();
    }
    if (iblSource == nullptr) {
        return glm::vec3(0.0f);
    }

    const glm::vec3 safeNormal = NormalizeDirectionSafe(worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 diffuseIblColor = iblSource->sample(safeNormal);
    return std::max(scene.iblSettings.diffuseIntensity, 0.0f) * diffuseIblColor;
}

/**
 * @brief Schlick 近似（粗糙度修正版本），用于计算 IBL 镜面菲涅耳。
 */
glm::vec3 FresnelSchlickRoughness(float cosTheta, const glm::vec3& F0, float roughness)
{
    const float safeCosTheta = std::clamp(cosTheta, 0.0f, 1.0f);
    const float oneMinusCos = 1.0f - safeCosTheta;
    const float fresnelFactor = oneMinusCos * oneMinusCos * oneMinusCos * oneMinusCos * oneMinusCos;
    const float oneMinusRoughness = std::max(1.0f - roughness, 0.0f);

    const glm::vec3 maxF(
        std::max(oneMinusRoughness, F0.r),
        std::max(oneMinusRoughness, F0.g),
        std::max(oneMinusRoughness, F0.b));

    return F0 + (maxF - F0) * fresnelFactor;
}

/**
 * @brief BRDF LUT 采样结果（x=scale, y=bias）。
 */
struct BrdfSample
{
    float scale = 1.0f;
    float bias = 0.0f;
};

/**
 * @brief 采样 BRDF LUT；当 LUT 缺失时使用简化近似，保证 Specular IBL 可回退。
 */
BrdfSample SampleBrdfLut(const Scene& scene, float ndotv, float roughness)
{
    const float safeNdotV = std::clamp(ndotv, 0.0f, 1.0f);
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);

    if (scene.iblBrdfLut && scene.iblBrdfLut->valid()) {
        // BRDF LUT 约定为 x=roughness, y=NdotV，采样顺序必须与离线生成保持一致。
        const glm::vec3 lutSample = scene.iblBrdfLut->sample(safeRoughness, safeNdotV);
        BrdfSample result;
        result.scale = std::clamp(lutSample.r, 0.0f, 1.0f);
        result.bias = std::clamp(lutSample.g, 0.0f, 1.0f);
        return result;
    }

    // LUT 缺失时用轻量近似，避免直接硬回退到 0 导致镜面 IBL 完全消失。
    const float oneMinusNdotV = 1.0f - safeNdotV;
    const float fresnelPow5 = oneMinusNdotV * oneMinusNdotV * oneMinusNdotV * oneMinusNdotV * oneMinusNdotV;

    BrdfSample fallback;
    fallback.scale = std::clamp(1.0f - 0.55f * safeRoughness, 0.0f, 1.0f);
    fallback.bias = std::clamp(0.02f + 0.50f * safeRoughness * fresnelPow5, 0.0f, 1.0f);
    return fallback;
}

/**
 * @brief 计算 Specular IBL 分量（Split-Sum：Prefilter * BRDF LUT）。
 */
glm::vec3 ComputeSpecularIblLighting(
    const Scene& scene,
    const glm::vec3& worldNormal,
    const glm::vec3& viewDir,
    const glm::vec3& albedo,
    const float roughness,
    const float metallic)
{
    if (!scene.iblSettings.enableIBL || !scene.iblSettings.enableSpecularIBL) {
        return glm::vec3(0.0f);
    }

    const float safeIntensity = std::max(scene.iblSettings.specularIntensity, 0.0f);
    if (safeIntensity <= 0.0f) {
        return glm::vec3(0.0f);
    }

    const glm::vec3 safeNormal = NormalizeDirectionSafe(worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 safeViewDir = NormalizeDirectionSafe(viewDir, glm::vec3(0.0f, 0.0f, 1.0f));
    const float safeRoughness = std::clamp(roughness, 0.0f, 1.0f);
    const float safeMetallic = std::clamp(metallic, 0.0f, 1.0f);

    const int requestedLod = SelectSpecularLod(scene, safeRoughness);
    const float lod = (scene.iblSettings.specularLodMode == SpecularLodMode::Manual)
        ? static_cast<float>(requestedLod)
        : ComputeSpecularLodLevel(safeRoughness);

    const float ndotv = std::clamp(glm::dot(safeNormal, safeViewDir), 0.0f, 1.0f);

    const glm::vec3 reflectDir = NormalizeDirectionSafe(
        glm::reflect(-safeViewDir, safeNormal),
        glm::vec3(0.0f, 0.0f, 1.0f));

    glm::vec3 prefilteredColor(0.0f);

    const auto& specularLodMap = scene.IBLSpecLodMaps[requestedLod];
    if (specularLodMap && specularLodMap->valid()) {
        prefilteredColor = specularLodMap->sample(reflectDir);
    } else if (scene.iblPrefilterMap && scene.iblPrefilterMap->valid()) {
        prefilteredColor = scene.iblPrefilterMap->sampleLod(reflectDir, lod);
    } else if (scene.skyboxTexture && scene.skyboxTexture->valid()) {
        prefilteredColor = scene.skyboxTexture->sampleLod(reflectDir, lod);
    } else {
        return glm::vec3(0.0f);
    }

    //splitsum的BRDF LUT采样，得到 scale 和 bias
    const BrdfSample brdf = SampleBrdfLut(scene, ndotv, safeRoughness);
    const glm::vec3 F0 = glm::vec3(0.04f) * (1.0f - safeMetallic) + albedo * safeMetallic;
    const glm::vec3 fresnel = FresnelSchlickRoughness(ndotv, F0, safeRoughness);

    const glm::vec3 specularIbl = prefilteredColor * (fresnel * brdf.scale + glm::vec3(brdf.bias));
    return safeIntensity * specularIbl;
}

/**
 * @brief 汇总 IBL 环境光（Diffuse + Specular）。
 * @param albedo 反照率颜色，分别参与 Diffuse IBL 与 Specular IBL 的能量计算。
 */
glm::vec3 ComputeIblLighting(
    const Scene& scene,
    const glm::vec3& worldNormal,
    const glm::vec3& viewDir,
    const glm::vec3& albedo,
    std::weak_ptr<Material> material = std::weak_ptr<Material>())
{
    if (!scene.iblSettings.enableIBL) {
        return glm::vec3(0.0f);
    }

    const glm::vec3 safeNormal = NormalizeDirectionSafe(worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 safeViewDir = NormalizeDirectionSafe(viewDir, glm::vec3(0.0f, 0.0f, 1.0f));

    float roughness = 0.5f;
    float metallic = 0.0f;
    if (const auto& mat = material.lock()) {
        roughness = std::clamp(mat->roughness, 0.0f, 1.0f);
        metallic = std::clamp(mat->metallic, 0.0f, 1.0f);
    }

    const glm::vec3 diffuseIbl = ComputeDiffuseIblLighting(scene, safeNormal);
    if (!scene.iblSettings.enableSpecularIBL) {
        return albedo * diffuseIbl;
    }

    const float ndotv = std::clamp(glm::dot(safeNormal, safeViewDir), 0.0f, 1.0f);

    const glm::vec3 F0 = glm::vec3(0.04f) * (1.0f - metallic) + albedo * metallic;
    const glm::vec3 kS = FresnelSchlickRoughness(ndotv, F0, roughness);
    const glm::vec3 kD = (glm::vec3(1.0f) - kS) * (1.0f - metallic);

    const glm::vec3 specularIbl = ComputeSpecularIblLighting(scene, safeNormal, safeViewDir, albedo, roughness, metallic);
    return albedo * (kD * diffuseIbl) + specularIbl;
}

/**
 * @brief 在世界坐标下执行 Blinn-Phong 光照并写回最终像素。
 * @param colorbuffer 最终颜色缓冲，函数会把结果写入 payload.bufferIndex。
 * @param payload 当前片元输入（颜色、法线、世界坐标、阴影可见性等）。
 */
void BlingPhongShader(std::vector<std::uint32_t>& colorbuffer, const Fragment& payload, const Scene& scene)
{
    if (!scene.camera) {
        colorbuffer[payload.bufferIndex] = SoftwareRenderer::packColor(payload.color);
        return;
    }

    const glm::vec3 albedo = glm::vec3(payload.color.r, payload.color.g, payload.color.b) / 255.0f;

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

    const glm::vec3 constantAmbientLighting = ComputeConstantAmbientLighting(scene);
    // const glm::vec3 iblLighting = ComputeIblLighting(scene, normal, viewDir, albedo, payload.material);
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
    glm::vec3 finalLinear = albedo * constantAmbientLighting;
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

void CookTorrance_Shader(std::vector<std::uint32_t>& colorbuffer, const Fragment& payload, const Scene& scene)
{
    if (!scene.camera) {
        colorbuffer[payload.bufferIndex] = SoftwareRenderer::packColor(payload.color);
        return;
    }

    glm::vec3 albedo = glm::vec3(payload.color.r, payload.color.g, payload.color.b) / 255.0f;

    glm::vec3 normal = NormalizeDirectionSafe(payload.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 viewDir = NormalizeDirectionSafe(scene.camera->position() - payload.worldPos, glm::vec3(0.0f, 0.0f, 1.0f));

    const glm::vec3 constantAmbientLighting = ComputeConstantAmbientLighting(scene);
    const glm::vec3 iblColor = ComputeIblLighting(scene, normal, viewDir, albedo, payload.material);

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
            attenuation = 1.0f / (1.0f + 0.14f * distanceToLight + 0.07f * distanceToLight * distanceToLight);

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

    // 直接光照继续受阴影控制，IBL 与常量环境光不受阴影压制。
    const float shadowVisibility = std::clamp(payload.shadowVisibility, 0.0f, 1.0f);
    glm::vec3 finalLinear = albedo * constantAmbientLighting;
    finalLinear += iblColor;
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
    scene.iblIrradianceMap.reset();
    scene.IBLSpecLodMaps = {};
    scene.IBLSpecLodFallbackSources = {{-1, -1, -1, -1, -1, -1}};
    scene.iblSpecLodRootPath.clear();
    scene.iblPrefilterMap.reset();
    scene.iblBrdfLut.reset();
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
    renderer.SetFragmentShader(CookTorrance_Shader);

    std::cout << "Wireframe overlay: OFF (press F1 to toggle)" << '\n';
    std::cout << "Back-face culling: ON (press F2 to toggle)" << '\n';
    std::cout << "MSAA samples: " << renderer.msaaSampleCount() << "x (press F3 to cycle 1x/2x/4x)" << '\n';
    std::cout << "Fragment shader: CookTorrance (IBL enabled path)" << '\n';
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
        if (currentSkyboxIndex >= 0 && requestedPreset == ScenePreset::Scene2DualSphereFloorPoint) {
            (void)ApplySkyboxByIndex(scene, skyboxAssets, currentSkyboxIndex);
        }
        if (requestedPreset == ScenePreset::Scene1MaryFloorPoint) {
            scene.enableSkybox = false;
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
    int& currentSkyboxIndex,
    ScenePreset currentScenePreset)
{
    if (!debugUI.hasPendingSkyboxSwitch()) {
        // 场景一固定关闭天空盒，避免在该场景误开启背景渲染。
        if (currentScenePreset == ScenePreset::Scene1MaryFloorPoint) {
            scene.enableSkybox = false;
        }
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

    if (currentScenePreset == ScenePreset::Scene1MaryFloorPoint) {
        scene.enableSkybox = false;
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
    if (currentScenePreset == ScenePreset::Scene1MaryFloorPoint) {
        scene.enableSkybox = false;
    }

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

        ProcessPendingSkyboxSwitch(scene, debugUI, skyboxAssets, currentSkyboxIndex, currentScenePreset);

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
