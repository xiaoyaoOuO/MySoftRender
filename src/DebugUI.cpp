#include "DebugUI.h"

#include "Scene.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <algorithm>

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

void DebugUI::drawShadowPanel(Scene& scene)
{
    if (!initialized_ || !visible_) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Shadow Debug", &visible_)) {
        ImGui::TextUnformatted("Scene Shadow Settings");
        ImGui::Separator();

        ImGui::Checkbox("Enable ShadowMap", &scene.shadowSettings.enableShadowMap);
        ImGui::SliderFloat("Depth Bias", &scene.shadowSettings.depthBias, 0.0f, 0.02f, "%.6f", ImGuiSliderFlags_Logarithmic);

        // 作用：约束可视化调参结果，避免误设为负值导致阴影测试逻辑反转。
        // 用法：拖动滑杆后自动进行一次下限保护。
        scene.shadowSettings.depthBias = std::max(scene.shadowSettings.depthBias, 0.0f);

        ImGui::Spacing();
        ImGui::TextUnformatted("Hotkeys:");
        ImGui::BulletText("F4: Toggle this panel");
        ImGui::BulletText("F5: Toggle mouse capture");

        if (!scene.lights.empty() && scene.lights[0]) {
            const auto& light = *scene.lights[0];
            const auto& lightPos = light.position();
            const auto& lightDir = light.direction();
            ImGui::Spacing();
            ImGui::Text("Main Light Pos: (%.2f, %.2f, %.2f)", lightPos.x, lightPos.y, lightPos.z);
            ImGui::Text("Main Light Dir: (%.2f, %.2f, %.2f)", lightDir.x, lightDir.y, lightDir.z);
        }
    }
    ImGui::End();
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
