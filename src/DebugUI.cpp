#include "DebugUI.h"

#include "Scene.h"
#include "software_renderer.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
// 把任意方向向量安全归一化，避免零向量导致非法光照方向。传入 UI 编辑后的方向向量，返回可直接写回 Light 的单位向量。
glm::vec3 NormalizeDirectionSafe(const glm::vec3& dir)
{
    if (glm::dot(dir, dir) <= 1e-10f) {
        return glm::vec3(0.0f, -1.0f, 0.0f);
    }
    return glm::normalize(dir);
}

// 对缩放值做下限保护，避免模型缩放到 0 或负数导致矩阵异常。在模型缩放 UI 修改后调用，再写回 Object::setScale。
glm::vec3 ClampScaleMin(const glm::vec3& scaleValue)
{
    constexpr float kMinScale = 0.01f;
    return glm::vec3(
        std::max(scaleValue.x, kMinScale),
        std::max(scaleValue.y, kMinScale),
        std::max(scaleValue.z, kMinScale));
}

// 约束场景预设索引，避免 UI 状态被非法值污染。
int ClampScenePresetIndex(int presetIndex)
{
    return std::clamp(presetIndex, 0, 1);
}
}

DebugUI::~DebugUI()
{
    shutdown();
}

bool DebugUI::initialize(SDL_Window* window, SDL_Renderer* renderer)
{
    if (initialized_) {
        return true;
    }
    if (window == nullptr || renderer == nullptr) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    renderer_ = renderer;
    initialized_ = true;
    return true;
}

void DebugUI::shutdown()
{
    if (!initialized_) {
        return;
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    renderer_ = nullptr;
    initialized_ = false;
}

void DebugUI::processEvent(const SDL_Event& event)
{
    if (!initialized_) {
        return;
    }
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void DebugUI::beginFrame()
{
    if (!initialized_) {
        return;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::drawShadowPanel(Scene& scene, SoftwareRenderer& renderer)
{
    if (!initialized_ || !visible_) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Renderer Control", &visible_)) {
        ImGui::SeparatorText("Scene");

        const char* scenePresetItems[] = {
            "Scene 1: Mary + Floor + Point Light",
            "Scene 2: Dual Spheres + Floor + Point Light"
        };

        int scenePresetIndex = ClampScenePresetIndex(currentScenePreset_);
        if (ImGui::Combo("Scene Preset", &scenePresetIndex, scenePresetItems, IM_ARRAYSIZE(scenePresetItems))) {
            // 仅记录切换请求，由主循环在安全点完成场景重建，避免 UI 层直接改写 Scene 生命周期。
            scenePresetIndex = ClampScenePresetIndex(scenePresetIndex);
            if (scenePresetIndex != currentScenePreset_) {
                currentScenePreset_ = scenePresetIndex;
                pendingScenePreset_ = scenePresetIndex;
            }
        }

        ImGui::Text("Objects: %d", static_cast<int>(scene.objects.size()));
        ImGui::SameLine();
        ImGui::Text("Lights: %d", static_cast<int>(scene.lights.size()));
        if (pendingScenePreset_ >= 0) {
            ImGui::TextUnformatted("Scene switch queued. It will apply this frame.");
        }

        if (ImGui::BeginTabBar("DebugTabs")) {
            if (ImGui::BeginTabItem("Shadow")) {
            ImGui::Checkbox("Enable ShadowMap", &scene.shadowSettings.enableShadowMap);
            ImGui::SliderInt("Shadow Resolution", &scene.shadowSettings.shadowMapResolution, 128, 2048);
            ImGui::SliderFloat("Depth Bias", &scene.shadowSettings.depthBias, 0.0f, 0.02f, "%.6f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Normal Bias", &scene.shadowSettings.normalBias, 0.0f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);

            const char* filterModeItems[] = {"Hard", "PCF", "PCSS"};
            int filterMode = static_cast<int>(scene.shadowSettings.filterMode);
            if (ImGui::Combo("Filter Mode", &filterMode, filterModeItems, IM_ARRAYSIZE(filterModeItems))) {
                scene.shadowSettings.filterMode = static_cast<ShadowFilterMode>(filterMode);
            }

            if (scene.shadowSettings.filterMode == ShadowFilterMode::PCF) {
                ImGui::SeparatorText("PCF");
                ImGui::SliderInt("PCF Kernel Radius", &scene.shadowSettings.pcfKernelRadius, 1, 8);
                ImGui::SliderInt("PCF Sample Count", &scene.shadowSettings.pcfSampleCount, 4, 128);
            }

            if (scene.shadowSettings.filterMode == ShadowFilterMode::PCSS) {
                ImGui::SeparatorText("PCSS");
                ImGui::SliderInt("PCSS Blocker Samples", &scene.shadowSettings.pcssBlockerSearchSamples, 4, 128);
                ImGui::SliderInt("PCSS Filter Samples", &scene.shadowSettings.pcssFilterSamples, 8, 256);
                ImGui::SliderFloat("PCSS Light Size", &scene.shadowSettings.pcssLightSize, 0.001f, 0.2f, "%.4f", ImGuiSliderFlags_Logarithmic);
            }

            // 约束可视化调参结果，避免误设为负值导致阴影测试逻辑反转。拖动滑杆后自动进行一次下限保护。
            scene.shadowSettings.depthBias = std::max(scene.shadowSettings.depthBias, 0.0f);
            scene.shadowSettings.normalBias = std::max(scene.shadowSettings.normalBias, 0.0f);
            scene.shadowSettings.shadowMapResolution = std::max(scene.shadowSettings.shadowMapResolution, 128);
            scene.shadowSettings.pcfKernelRadius = std::max(scene.shadowSettings.pcfKernelRadius, 1);
            scene.shadowSettings.pcfSampleCount = std::max(scene.shadowSettings.pcfSampleCount, 1);
            scene.shadowSettings.pcssBlockerSearchSamples = std::max(scene.shadowSettings.pcssBlockerSearchSamples, 1);
            scene.shadowSettings.pcssFilterSamples = std::max(scene.shadowSettings.pcssFilterSamples, 1);
            scene.shadowSettings.pcssLightSize = std::max(scene.shadowSettings.pcssLightSize, 0.0001f);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Light")) {
            if (!scene.lights.empty() && scene.lights[0]) {
                Light& light = *scene.lights[0];

                const char* lightTypeItems[] = {"Point", "Directional", "Spot"};
                int lightType = static_cast<int>(light.type());
                if (ImGui::Combo("Light Type", &lightType, lightTypeItems, IM_ARRAYSIZE(lightTypeItems))) {
                    light.setType(static_cast<Light::LightType>(lightType));
                }

                glm::vec3 lightPos = light.position();
                if (ImGui::DragFloat3("Position##light", &lightPos.x, 0.05f)) {
                    light.setPosition(lightPos);
                }

                glm::vec3 lightDir = light.direction();
                if (ImGui::DragFloat3("Direction##light", &lightDir.x, 0.01f, -1.0f, 1.0f)) {
                    light.setDirection(NormalizeDirectionSafe(lightDir));
                }

                glm::vec3 lightColor = light.color();
                if (ImGui::ColorEdit3("Color##light", &lightColor.x)) {
                    light.setColor(lightColor);
                }

                float lightIntensity = light.intensity();
                if (ImGui::DragFloat("Intensity##light", &lightIntensity, 0.05f, 0.0f, 20.0f, "%.2f")) {
                    light.setIntensity(std::max(lightIntensity, 0.0f));
                }

                bool castShadow = light.castShadowEnabled();
                if (ImGui::Checkbox("Cast Shadow##light", &castShadow)) {
                    light.setCastShadowEnabled(castShadow);
                }

                if (light.type() == Light::LightType::Directional) {
                    float orthoHalfSize = light.shadowOrthoHalfSize();
                    if (ImGui::DragFloat("Ortho Half Size##light", &orthoHalfSize, 0.05f, 0.1f, 50.0f, "%.2f")) {
                        light.setShadowOrthoHalfSize(orthoHalfSize);
                    }
                }

                float nearPlane = light.shadowNearPlane();
                if (ImGui::DragFloat("Near Plane##light", &nearPlane, 0.01f, 0.01f, 50.0f, "%.3f")) {
                    light.setShadowNearPlane(nearPlane);
                }

                float farPlane = light.shadowFarPlane();
                if (ImGui::DragFloat("Far Plane##light", &farPlane, 0.05f, 0.05f, 100.0f, "%.2f")) {
                    light.setShadowFarPlane(farPlane);
                }

                // 一键把光源摆到选中模型前上方，并自动朝向模型中心。先在模型区选择对象，再点击按钮可快速得到完整落地阴影。
                if (!scene.objects.empty()
                    && selectedObjectIndex_ >= 0
                    && selectedObjectIndex_ < static_cast<int>(scene.objects.size())
                    && ImGui::Button("Light To Front-Upper Of Selected")) {
                    const glm::vec3 targetPos = scene.objects[static_cast<size_t>(selectedObjectIndex_)]->getPosition();
                    const glm::vec3 newLightPos = targetPos + glm::vec3(0.0f, 3.2f, 2.2f);
                    light.setPosition(newLightPos);
                    light.setDirection(NormalizeDirectionSafe(targetPos - newLightPos));
                }
            } else {
                ImGui::TextUnformatted("No light in scene.");
            }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Model")) {
            if (!scene.objects.empty()) {
                const int objectCount = static_cast<int>(scene.objects.size());
                if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= objectCount) {
                    selectedObjectIndex_ = 0;
                }

                ImGui::SliderInt("Selected Object", &selectedObjectIndex_, 0, objectCount - 1);
                ImGui::Text("Editing object index: %d", selectedObjectIndex_);

                Object* selectedObject = scene.objects[static_cast<size_t>(selectedObjectIndex_)].get();
                if (selectedObject != nullptr) {
                    glm::vec3 pos = selectedObject->getPosition();
                    if (ImGui::DragFloat3("Position##model", &pos.x, 0.02f)) {
                        selectedObject->setPosition(pos);
                    }

                    glm::vec3 rot = selectedObject->getRotation();
                    if (ImGui::DragFloat3("Rotation##model", &rot.x, 0.5f)) {
                        selectedObject->setRotation(rot);
                    }

                    glm::vec3 scale = selectedObject->getScale();
                    if (ImGui::DragFloat3("Scale##model", &scale.x, 0.01f)) {
                        selectedObject->setScale(ClampScaleMin(scale));
                    }
                }
            } else {
                ImGui::TextUnformatted("No model in scene.");
            }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Threading")) {
            bool enableFragmentMultithreading = renderer.fragmentMultithreadingEnabled();
            if (ImGui::Checkbox("Enable Fragment Multithreading", &enableFragmentMultithreading)) {
                renderer.setFragmentMultithreadingEnabled(enableFragmentMultithreading);
            }

            const FragmentThreadingStats& stats = renderer.fragmentThreadingStats();
            const unsigned long long poolThreadCount = static_cast<unsigned long long>(stats.poolThreadCount);
            const unsigned long long activeWorkerCount = static_cast<unsigned long long>(stats.activeWorkerCount);
            const unsigned long long pendingTaskCount = static_cast<unsigned long long>(stats.pendingTaskCount);
            const unsigned long long scheduledTaskCount = static_cast<unsigned long long>(stats.scheduledTaskCount);
            const unsigned long long dispatchedWorkerCount = static_cast<unsigned long long>(stats.dispatchedWorkerCount);
            const unsigned long long fragmentCount = static_cast<unsigned long long>(stats.fragmentCount);

            // 同时展示线程池“配置状态”和“本帧实际调度结果”，便于快速对比开关前后的效率变化。
            ImGui::Text("Pool Threads: %llu", poolThreadCount);
            ImGui::Text("Active Workers (now): %llu", activeWorkerCount);
            ImGui::Text("Pending Tasks (now): %llu", pendingTaskCount);
            ImGui::Text("Dispatched Workers (frame): %llu", dispatchedWorkerCount);
            ImGui::Text("Scheduled Tasks (frame): %llu", scheduledTaskCount);
            ImGui::Text("Fragments (frame): %llu", fragmentCount);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Status")) {
                ImGui::TextUnformatted("Hotkeys:");
                ImGui::BulletText("F4: Toggle this panel");
                ImGui::BulletText("F5: Toggle mouse capture");

                if (scene.camera) {
                    const glm::vec3 cameraPos = scene.camera->position();
                    const glm::vec3 cameraTarget = scene.camera->target();
                    ImGui::Spacing();
                    ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
                    ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)", cameraTarget.x, cameraTarget.y, cameraTarget.z);
                }

                if (!scene.lights.empty() && scene.lights[0]) {
                    const auto& light = *scene.lights[0];
                    const auto& lightPos = light.position();
                    const auto& lightDir = light.direction();
                    ImGui::Spacing();
                    ImGui::Text("Main Light Pos: (%.2f, %.2f, %.2f)", lightPos.x, lightPos.y, lightPos.z);
                    ImGui::Text("Main Light Dir: (%.2f, %.2f, %.2f)", lightDir.x, lightDir.y, lightDir.z);
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DebugUI::setCurrentScenePreset(int presetIndex)
{
    currentScenePreset_ = ClampScenePresetIndex(presetIndex);
    pendingScenePreset_ = -1;
}

bool DebugUI::hasPendingSceneSwitch() const
{
    return pendingScenePreset_ >= 0;
}

int DebugUI::consumePendingSceneSwitch()
{
    const int requestedScenePreset = pendingScenePreset_;
    pendingScenePreset_ = -1;
    return requestedScenePreset;
}

void DebugUI::render()
{
    if (!initialized_ || renderer_ == nullptr) {
        return;
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

bool DebugUI::wantsMouseCapture() const
{
    if (!initialized_) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::wantsKeyboardCapture() const
{
    if (!initialized_) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}
