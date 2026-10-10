#include <vultra/main/app/research_project_app.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>

namespace vultra
{
    namespace
    {
        struct ResolutionPreset
        {
            const char* name;
            Extent      size;
        };

        // Per-eye render extents used in the study; these do not emulate headset projections.
        constexpr std::array<ResolutionPreset, 6> kResolutionPresets {{
            {"Desktop fast - 1024 x 1024", {1024, 1024}},
            {"Pipeline figure - 1280 x 1280", {1280, 1280}},
            {"Reference - 1440 x 1600", {1440, 1600}},
            {"Valve Index - 2016 x 2240", {2016, 2240}},
            {"Pimax Crystal - 3234 x 3826", {3234, 3826}},
            {"Pimax 8K X (Large FOV) - 6254 x 2962", {6254, 2962}},
        }};
    } // namespace

    void ResearchProjectApp::buildRendererSettings()
    {
        if (!ImGui::Begin("Renderer Settings"))
        {
            ImGui::End();
            return;
        }
        auto  ui       = m_Gui.frame();
        auto& settings = m_RenderSettings;
        bool  changed  = false;
        if (ImGui::CollapsingHeader("Render resolution", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(bool(m_Session));
            if (ui.checkbox("Match viewport", &m_MatchViewport))
            {
                m_ViewportStable = 0;
            }
            ImGui::EndDisabled();
            if (m_MatchViewport && !m_Session)
            {
                ImGui::TextWrapped("Applies the per-eye panel size after three stable frames. Disable for fixed "
                                   "experiment resolutions; captures and benchmarks record the actual size.");
            }
            if (m_Session)
            {
                float scale = m_XrRenderScale;
                if (ui.sliderFloat("Per-eye render scale", &scale, 0.25f, 1, "%.2fx"))
                {
                    m_XrRenderScale = scale;
                    changed         = true;
                }
                ImGui::TextDisabled("Runtime eye: %u x %u", m_NativeEyeSizes[0].width, m_NativeEyeSizes[0].height);
                ImGui::TextWrapped("OpenXR owns the eye size and projection. Desktop presets do not override them.");
            }
            else
            {
                const Extent draft {uint32_t(m_ResolutionDraft[0]), uint32_t(m_ResolutionDraft[1])};
                const auto   preset = std::ranges::find(kResolutionPresets, draft, &ResolutionPreset::size);
                if (ui.beginCombo("Resolution preset", preset == kResolutionPresets.end() ? "Custom" : preset->name))
                {
                    for (const auto& option : kResolutionPresets)
                    {
                        if (ui.selectable(option.name, option.size == draft))
                        {
                            m_MatchViewport   = false;
                            m_Options.eyeSize = option.size;
                            m_DesktopSizes    = {option.size, option.size};
                            m_HeadsetProfile.reset();
                            m_ResolutionDraft = {int(option.size.width), int(option.size.height)};
                            changed           = true;
                        }
                    }
                    ui.endCombo();
                }
                if (EditorGuiLayout::beginProperty("Per-eye size"))
                {
                    ImGui::InputInt2("##extent", m_ResolutionDraft.data());
                    EditorGuiLayout::endProperty();
                }
                const bool valid = m_ResolutionDraft[0] >= 11 && m_ResolutionDraft[0] <= 8192 &&
                                   m_ResolutionDraft[1] >= 11 && m_ResolutionDraft[1] <= 8192;
                ImGui::BeginDisabled(!valid);
                if (ImGui::Button("Apply custom resolution"))
                {
                    m_MatchViewport   = false;
                    m_Options.eyeSize = {uint32_t(m_ResolutionDraft[0]), uint32_t(m_ResolutionDraft[1])};
                    m_DesktopSizes    = {m_Options.eyeSize, m_Options.eyeSize};
                    m_HeadsetProfile.reset();
                    changed = true;
                }
                ImGui::EndDisabled();
                if (!valid)
                {
                    ImGui::TextWrapped("Width and height must be between 11 and 8192.");
                }
                ImGui::TextWrapped("Presets set render size only; the camera FOV and eye separation stay unchanged.");
            }
            if (m_Renderer->ready())
            {
                const auto& size = m_Renderer->texture(StereoOutput::eLinearHdr, 1, 0).desc;
                ImGui::TextDisabled("Active per eye: %u x %u", size.width, size.height);
            }
        }
        if (ImGui::CollapsingHeader("Renderer"))
        {
            int path = settings.path == RenderPath::eNaiveForward ? 0 : 1;
            changed |= ui.combo("Render path", &path, "Forward\0Deferred\0");
            settings.path          = path ? RenderPath::eNaiveDeferred : RenderPath::eNaiveForward;
            const bool hasMeshlets = bool(m_Renderer->scene().meshlets);
            ImGui::BeginDisabled(!hasMeshlets);
            changed |= ui.checkbox("Mesh shading", &settings.meshShading);
            changed |= ui.checkbox("Meshlet frustum culling", &settings.meshletCulling);
            changed |= ui.checkbox("Meshlet colors", &settings.meshletColors);
            ImGui::EndDisabled();
            if (!hasMeshlets)
            {
                ImGui::TextWrapped("Mesh shading requires the project device feature and imported meshlets.");
            }
        }
        if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto direction = settings.directionToLight;
            if (ui.sliderFloat3("Direction to light", glm::value_ptr(direction), -1, 1))
            {
                if (glm::dot(direction, direction) > 0)
                {
                    settings.directionToLight = direction;
                    changed                   = true;
                }
            }
            if (EditorGuiLayout::beginProperty("Linear light color"))
            {
                changed |= ImGui::ColorEdit3("##color",
                                             glm::value_ptr(settings.lightColor),
                                             ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                EditorGuiLayout::endProperty();
            }
            changed |= ui.sliderFloat("Light intensity", &settings.lightIntensity, 0, 10);
        }
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ui.checkbox("Skybox", &settings.skybox);
            changed |= ui.checkbox("IBL", &settings.ibl);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Image-based lighting from the environment; independent of skybox visibility.");
            }
            changed |= ui.sliderFloat("Intensity", &settings.environmentIntensity, 0, 4);
            if (EditorGuiLayout::beginProperty("Constant ambient (linear)"))
            {
                changed |= ImGui::ColorEdit3("##ambient",
                                             glm::value_ptr(settings.ambientColor),
                                             ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                EditorGuiLayout::endProperty();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip(
                    "Approximate ambient fill: color times base color and material AO. Independent of IBL.");
            }
        }
        if (ImGui::CollapsingHeader("Shadows"))
        {
            changed |= ui.checkbox("Cache static maps", &settings.cacheShadows);
            guiTooltip(ui, "Reuse completed maps until light/camera, transforms, alpha material or shader changes.");
            int filter = int(settings.shadowFilter);
            changed |= ui.combo("Filtering", &filter, "Off\0Hard\0PCF\0PCSS\0");
            settings.shadowFilter = ShadowFilter(filter);
            const auto sizeLabel  = std::to_string(settings.shadowResolution);
            if (ui.beginCombo("Map resolution", sizeLabel.c_str()))
            {
                for (const uint32_t size : {256u, 512u, 1024u, 2048u, 4096u, 8192u})
                {
                    if (ui.selectable(std::to_string(size).c_str(), size == settings.shadowResolution))
                    {
                        settings.shadowResolution = size;
                        changed                   = true;
                    }
                }
                ui.endCombo();
            }
            changed |= ui.sliderFloat("Split lambda", &settings.splitLambda, 0, 1);
            changed |= ui.sliderFloat("Depth bias", &settings.shadowBias, 0, 0.003f, "%.5f");
            changed |= ui.sliderFloat("Normal bias", &settings.normalBias, 0, 3);
            changed |= ui.sliderFloat("Sun radius (rad)", &settings.sunAngularRadius, 0, 0.1f);
            changed |= ui.sliderFloat("Cascade blend", &settings.cascadeBlend, 0, 1);
        }
        if (ImGui::CollapsingHeader("Material and debug view"))
        {
            changed |= ui.sliderFloat("Roughness override", &settings.roughnessOverride, -1, 1);
            changed |= ui.sliderFloat("Metalness override", &settings.metalnessOverride, -1, 1);
            ImGui::TextWrapped("An override of -1 uses the authored material value.");
            int mode = int(settings.debugMode);
            changed |= ui.combo("View", &mode, "Lit\0Base color\0Normals\0Cascades\0Shadow visibility\0Emission\0");
            settings.debugMode = uint32_t(mode);
        }
        if (ImGui::CollapsingHeader("Camera"))
        {
            if (!m_Session)
            {
                ImGui::BeginDisabled(bool(m_HeadsetProfile));
                float fov = glm::degrees(m_Rig.verticalFov);
                if (ui.sliderFloat("Vertical FOV (deg)", &fov, 1, 179))
                {
                    m_Rig.verticalFov = glm::radians(fov);
                    changed           = true;
                }
                changed |= ui.sliderFloat("Eye separation (m)", &m_Ipd, 0.001f, 0.2f);
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::TextWrapped("Headset FOV and eye poses come from the OpenXR runtime.");
            }
            ui.sliderFloat("Move speed", &m_Rig.speed, 0.1f, 50, "%.1f m/s");
            ImGui::TextWrapped("RMB drag over the image to look; WASD / QE to move; Shift to move faster.");
        }
        if (ImGui::CollapsingHeader("Display"))
        {
            if (!m_Research.hasEditorControls())
            {
                ui.combo("Viewport", &m_Options.view, "Reference stereo\0Current stereo\0Comparison\0HDR error\0");
                if (m_Session)
                {
                    ui.combo("Headset output", &m_XrMethod, "Reference\0Current\0");
                }
            }
            changed |= ui.sliderFloat("Exposure (EV)", &settings.exposure, -4, 4);
            int tone = int(settings.toneOperator);
            changed |= ui.combo("Tone operator", &tone, "ACES\0None\0Reinhard\0");
            settings.toneOperator = ToneOperator(tone);
            ImGui::TextWrapped("Desktop: one sRGB transfer into UNORM. OpenXR float output stays linear. Raw HDR "
                               "metrics/captures precede exposure and tone mapping.");
            ui.sliderFloat("Error-map gain", &m_Renderer->differenceGain, 0.1f, 64, "%.1fx");
            ImGui::TextWrapped("Exposure and error-map gain change the display only.");
        }
        m_MetricsStale |= changed;
        if (!m_Status.empty())
        {
            ImGui::SeparatorText("Status");
            ImGui::TextWrapped("%s", m_Status.c_str());
        }
        const auto diagnostics = m_Research.diagnostics();
        if (!diagnostics.empty())
        {
            ImGui::SeparatorText("Shader diagnostics");
            ImGui::TextWrapped("%s", diagnostics.c_str());
        }
        if (ImGui::Button("Restore default docking"))
        {
            m_ResetDocking = true;
        }
        ImGui::End();
    }
} // namespace vultra
