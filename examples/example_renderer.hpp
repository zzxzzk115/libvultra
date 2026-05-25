#pragma once

#include <vultra/core/rhi/structs/render_backend_api.hpp>
#include <vultra/function/framegraph/framegraph_import.hpp>
#include <vultra/function/rendering/srp/builtin/features/builtin_screen_space_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/direct_gbuffer_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp>
#include <vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp>
#include <vultra/function/rendering/srp/builtin/passes/raytracing_primary_pass.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <vultra/function/services/frame_debugger_service.hpp>

#include <imgui.h>

#include <array>
#include <algorithm>
#include <functional>
#include <string_view>
#include <string>

namespace vultra::examples
{
    using ExampleImGuiCallback = std::function<void(Services)>;

    inline void suppressCameraWhenUsingImGui(Services services)
    {
        auto* cameraService = services.tryGet<ICameraService>();
        if (!cameraService)
            return;

        const bool suppress = ImGui::GetIO().WantCaptureMouse || ImGui::IsAnyItemHovered() ||
                              ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        cameraService->setCameraControlInputSuppressed(suppress);
    }

    inline void drawRenderDocCaptureButton(Services services)
    {
        auto* frameDebugger = services.tryGet<IFrameDebuggerService>();
        if (!frameDebugger || !frameDebugger->isAvailable())
            return;

        if (ImGui::Button("Capture One Frame"))
        {
            frameDebugger->captureSingleFrame();
        }
    }

    inline void drawExampleRenderSettings(Services services, bool drawShadowSettings = true)
    {
        auto* renderService = services.tryGet<IRenderService>();
        if (!renderService)
            return;

        auto& settings = renderService->builtinRenderSettings();
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int debugViewMode = static_cast<int>(settings.pbrLighting.debugViewMode);
            if (ImGui::Combo("Debug View",
                             &debugViewMode,
                             "Lit\0Albedo\0Normal\0Metallic\0Roughness\0AO\0Linear Depth\0"))
                settings.pbrLighting.debugViewMode =
                    static_cast<PbrLightingSettings::DebugViewMode>(debugViewMode);
            ImGui::ColorEdit3("Ambient Color", &settings.pbrLighting.ambientColor.x);
            ImGui::SliderFloat("Ambient Intensity", &settings.pbrLighting.ambientIntensity, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Shadow Strength", &settings.pbrLighting.shadowStrength, 0.0f, 1.0f, "%.2f");
            if (settings.pbrLighting.environmentMap)
                ImGui::Checkbox("Show Skybox", &settings.pbrLighting.showSkybox);
            ImGui::Checkbox("Enable IBL", &settings.pbrLighting.enableIBL);
            if (settings.pbrLighting.enableIBL)
            {
                ImGui::ColorEdit3("IBL Color", &settings.pbrLighting.iblColor.x);
                ImGui::SliderFloat("IBL Intensity", &settings.pbrLighting.iblIntensity, 0.0f, 5.0f, "%.2f");
            }
        }

        if (ImGui::CollapsingHeader("Screen Space"))
        {
            ImGui::Checkbox("Enable SSR", &settings.ssr.enabled);
            if (settings.ssr.enabled)
            {
                ImGui::SliderFloat("SSR Factor", &settings.ssr.reflectionFactor, 0.0f, 2.0f, "%.2f");
                ImGui::SliderInt("SSR Steps", &settings.ssr.maxSteps, 4, 128);
                ImGui::SliderInt("SSR Refinement", &settings.ssr.binaryRefinement, 0, 16);
                ImGui::SliderFloat("SSR Stride", &settings.ssr.stride, 0.02f, 2.0f, "%.2f");
                ImGui::SliderFloat("SSR Thickness", &settings.ssr.thickness, 0.01f, 5.0f, "%.2f");
            }
            ImGui::Checkbox("Enable SSAO", &settings.ssao.enabled);
            if (settings.ssao.enabled)
            {
                ImGui::SliderFloat("SSAO Radius", &settings.ssao.radius, 0.0f, 10.0f, "%.2f");
                ImGui::SliderFloat("SSAO Bias", &settings.ssao.bias, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("SSAO Intensity", &settings.ssao.intensity, 0.0f, 4.0f, "%.2f");
                ImGui::SliderInt("SSAO Max Pixels", &settings.ssao.maxRadiusPixels, 4, 128);
                ImGui::SliderInt("SSAO Steps", &settings.ssao.stepCount, 2, 8);
                ImGui::SliderInt("SSAO Directions", &settings.ssao.directionCount, 4, 16);
            }
            ImGui::Checkbox("Enable FXAA", &settings.enableFXAA);
        }

        if (drawShadowSettings && ImGui::CollapsingHeader("Shadows"))
        {
            ImGui::Checkbox("Enable Shadows", &settings.shadow.enabled);
            ImGui::SliderFloat("Coverage", &settings.shadow.coverageRadius, 1.0f, 250.0f, "%.1f");
            ImGui::SliderFloat("Light Distance", &settings.shadow.lightDistance, 1.0f, 250.0f, "%.1f");
            ImGui::SliderFloat("Z Range", &settings.shadow.zRange, 1.0f, 500.0f, "%.1f");
            ImGui::SliderFloat("Depth Bias", &settings.shadow.depthBias, 0.0f, 0.02f, "%.5f");
            ImGui::SliderFloat("Normal Bias", &settings.shadow.normalBias, 0.0f, 0.2f, "%.4f");
            int filterMode = static_cast<int>(settings.shadow.filterMode);
            if (ImGui::Combo("Filter", &filterMode, "Hard\0PCF\0PCSS\0"))
                settings.shadow.filterMode = static_cast<ShadowRenderSettings::FilterMode>(filterMode);
        }
    }

    inline void drawNamedLightControls(Services services, std::string_view name)
    {
        auto* worldService = services.tryGet<IWorldService>();
        if (!worldService)
            return;

        auto& reg = worldService->world().registry();
        auto  view = reg.view<NameComponent, TransformComponent, LightComponent>();
        for (auto entity : view)
        {
            auto& nameComponent = view.get<NameComponent>(entity);
            if (nameComponent.name != name)
                continue;

            auto& transform = view.get<TransformComponent>(entity);
            auto& light     = view.get<LightComponent>(entity);
            if (ImGui::CollapsingHeader(nameComponent.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat3("Position", &transform.position.x, 0.05f, -100.0f, 100.0f);
                ImGui::ColorEdit3("Color", &light.color.x);
                ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 200.0f, "%.2f");
                ImGui::SliderFloat("Range", &light.range, 0.0f, 200.0f, "%.2f");
                if (light.kind == 3u)
                {
                    ImGui::SliderFloat("Width", &light.width, 0.01f, 10.0f, "%.2f");
                    ImGui::SliderFloat("Height", &light.height, 0.01f, 10.0f, "%.2f");
                }
                if (light.kind == 0u)
                    ImGui::Checkbox("Casts Shadow", &light.castsShadow);
            }
            return;
        }
    }

    class ExampleXrMirrorPanel
    {
    public:
        void draw(Services services)
        {
            auto* backendService = services.tryGet<IRenderBackendService>();
            auto* imguiService   = services.tryGet<IImGuiService>();
            if (!imguiService)
                return;

            if (!ImGui::CollapsingHeader("XR Mirror", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            if (!backendService)
            {
                ImGui::TextUnformatted("Render backend service unavailable.");
                return;
            }

            ImGui::Text("XR Enabled: %s", backendService->isXREnabled() ? "Yes" : "No");
            ImGui::Text("Mirror Enabled: %s", backendService->isXRMirrorEnabled() ? "Yes" : "No");

            if (!backendService->isXREnabled() || !backendService->isXRMirrorEnabled())
            {
                release(*imguiService);
                return;
            }

            const auto eyeViews = backendService->xrEyeViews();
            const auto count    = std::min<std::size_t>(eyeViews.size(), m_TextureIds.size());
            ImGui::Text("Eye Views: %zu", eyeViews.size());
            if (count == 0)
            {
                release(*imguiService);
                ImGui::TextUnformatted("No XR eye views for this frame.");
                return;
            }

            for (size_t i = 0; i < count; ++i)
            {
                const auto* texture = eyeViews[i].mirrorTarget;
                if (m_Textures[i] == texture && m_TextureIds[i] != 0)
                    continue;
                if (m_TextureIds[i])
                    imguiService->removeTexture(m_TextureIds[i]);
                m_Textures[i]    = texture;
                m_TextureIds[i]  = texture ? imguiService->addTexture(*texture) : 0;
            }

            if (ImGui::Button("Fit"))
                m_FitToPanel = true;
            ImGui::SameLine();
            if (ImGui::Button("1:1"))
            {
                m_FitToPanel = false;
                m_Scale      = 1.0f;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Single Eye", &m_SingleEye);
            if (m_SingleEye)
                ImGui::SliderInt("Eye", &m_EyeIndex, 0, static_cast<int>(count - 1));
            if (!m_FitToPanel)
                ImGui::SliderFloat("Scale", &m_Scale, 0.1f, 2.0f, "%.2fx");
            ImGui::SliderFloat("Preview Tint", &m_Tint, 0.25f, 1.0f, "%.2fx");

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float avail   = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            const float slot    = m_SingleEye ? avail : std::max(1.0f, (avail - spacing) * 0.5f);

            auto drawEye = [&](const size_t i) {
                if (i >= count)
                    return;

                if (!m_Textures[i])
                {
                    ImGui::Text("Eye %u has no mirror target.", eyeViews[i].eyeIndex);
                    return;
                }
                if (!m_TextureIds[i])
                {
                    ImGui::Text("Eye %u texture registration failed.", eyeViews[i].eyeIndex);
                    return;
                }

                const auto  extent = m_Textures[i]->getExtent();
                const float width  = static_cast<float>(std::max(extent.width, 1u));
                const float height = static_cast<float>(std::max(extent.height, 1u));
                const float drawWidth = m_FitToPanel ? slot : std::min(slot, width * m_Scale);
                const ImVec2 imageSize {drawWidth, drawWidth * (height / width)};
                const ImVec4 tint {m_Tint, m_Tint, m_Tint, 1.0f};
                ImGui::BeginGroup();
                ImGui::Text("Eye %u  %ux%u", eyeViews[i].eyeIndex, extent.width, extent.height);
                ImGui::ImageWithBg(
                    m_TextureIds[i], imageSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0, 0, 0, 0), tint);
                ImGui::EndGroup();
            };

            if (m_SingleEye)
            {
                drawEye(static_cast<size_t>(std::clamp(m_EyeIndex, 0, static_cast<int>(count - 1))));
            }
            else
            {
                drawEye(0);
                if (count > 1)
                {
                    ImGui::SameLine();
                    drawEye(1);
                }
            }
        }

        void release(IImGuiService& imguiService)
        {
            for (auto& id : m_TextureIds)
            {
                if (id)
                    imguiService.removeTexture(id);
                id = 0;
            }
            m_Textures.fill(nullptr);
        }

    private:
        std::array<IImGuiService::TextureID, 2> m_TextureIds {0, 0};
        std::array<const rhi::Texture*, 2>      m_Textures {nullptr, nullptr};
        bool                                    m_FitToPanel {true};
        bool                                    m_SingleEye {true};
        int                                     m_EyeIndex {0};
        float                                   m_Scale {1.0f};
        float                                   m_Tint {1.0f};
    };

    class ExampleUniversalRenderer final : public FeatureRenderer
    {
    public:
        explicit ExampleUniversalRenderer(std::string title,
                                          ExampleImGuiCallback callback = {},
                                          bool drawXrMirror = false) :
            m_Title(std::move(title)), m_Callback(std::move(callback)), m_DrawXrMirror(drawXrMirror)
        {}

        std::string_view name() const override { return "universal"; }

        void init() override
        {
            auto* services = getServices();
            if (!services)
                return;

            auto& renderService = services->require<IRenderService>();
            const auto backendApi = services->require<IRenderBackendService>().renderDevice().getBackendApi();
            if (backendApi == rhi::RenderBackendApi::eWebGPU)
            {
                emplaceFeature<CompatibilityBaseColorFeature>();
                emplaceFeature<GeneralGaussianSplatFeature>();
                emplaceFeature<BuiltinScreenSpaceFeature>(renderService);
                emplaceFeature<FinalCompositionFeature>();
                return;
            }

            emplaceFeature<DirectGBufferFeature>(renderService);
            emplaceFeature<GeneralGaussianSplatFeature>();
            emplaceFeature<BuiltinScreenSpaceFeature>(renderService);
            emplaceFeature<FinalCompositionFeature>();
        }

        void onImGui() override
        {
            auto* services = getServices();
            if (!services)
                return;

            suppressCameraWhenUsingImGui(*services);

            ImGui::Begin(m_Title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            if (m_DrawXrMirror)
                m_XrMirror.draw(*services);
            if (m_Callback)
                m_Callback(*services);
            drawRenderDocCaptureButton(*services);
            ImGui::End();
        }

    private:
        std::string           m_Title;
        ExampleImGuiCallback  m_Callback;
        bool                  m_DrawXrMirror {false};
        ExampleXrMirrorPanel  m_XrMirror;
    };

    class ExampleRtRenderer final : public Renderer
    {
    public:
        explicit ExampleRtRenderer(std::string title, ExampleImGuiCallback callback = {}) :
            m_Title(std::move(title)), m_Callback(std::move(callback))
        {}

        std::string_view name() const override { return "universal_rt"; }

        void buildFrameGraph(FrameGraphBuildContext& ctx) override
        {
            const auto color = m_PrimaryPass.addPass(ctx);
            if (color)
            {
                const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
                m_FinalCompositionPass.compose(ctx, backBuffer);
            }
        }

        void onImGui() override
        {
            auto* services = getServices();
            if (!services)
                return;

            suppressCameraWhenUsingImGui(*services);

            ImGui::Begin(m_Title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            if (m_Callback)
                m_Callback(*services);
            drawRenderDocCaptureButton(*services);
            ImGui::End();
        }

    private:
        std::string           m_Title;
        ExampleImGuiCallback  m_Callback;
        RayTracingPrimaryPass m_PrimaryPass;
        FinalCompositionPass  m_FinalCompositionPass;
    };
} // namespace vultra::examples
