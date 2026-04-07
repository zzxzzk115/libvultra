#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/rendering/srp/builtin/features/compatibility_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#ifdef VULTRA_ENABLE_RENDERDOC
#include "vultra/function/services/frame_debugger_service.hpp"
#endif

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>

namespace vultra
{
    namespace
    {
        void drawHintRow(const char* icon, const char* text)
        {
            ImGui::TextUnformatted(icon);
            ImGui::SameLine();
            ImGui::TextUnformatted(text);
        }

        void syncImGuiTextureRegistration(IImGuiService&            imguiService,
                                          const rhi::Texture*       texture,
                                          const rhi::Texture*&      registeredTexture,
                                          IImGuiService::TextureID& textureId)
        {
            const bool alreadyCleared = registeredTexture == nullptr && texture == nullptr && textureId == 0;
            if (alreadyCleared)
                return;
            const bool sameTexture = texture != nullptr && textureId != 0 && registeredTexture == texture;
            if (sameTexture)
                return;

            if (textureId)
                imguiService.removeTexture(textureId);

            registeredTexture = texture;
            textureId         = texture ? imguiService.addTexture(*texture) : 0;
        }

        void drawFpsOverlay()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (!viewport)
                return;

            constexpr float kPadding = 10.0f;

            const ImVec2 windowPos {viewport->WorkPos.x + viewport->WorkSize.x - kPadding,
                                    viewport->WorkPos.y + kPadding};
            const ImVec2 windowPivot {1.0f, 0.0f};

            ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPivot);
            ImGui::SetNextWindowBgAlpha(0.35f);

            constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin("##UniversalRendererFpsOverlay", nullptr, kFlags))
            {
                const float fps = ImGui::GetIO().Framerate;
                const float ms  = fps > 0.0f ? (1000.0f / fps) : 0.0f;

                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("Frame: %.2f ms", ms);
            }
            ImGui::End();
        }

        void drawCameraHintOverlay(const std::optional<CameraControlOverlayInfo>& infoOpt)
        {
            if (!infoOpt.has_value())
                return;
            const auto info = *infoOpt;
            if (!info.enabled)
                return;

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (!viewport)
                return;

            constexpr float kPadding = 10.0f;
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + kPadding, viewport->WorkPos.y + kPadding), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin("##CameraControlHintsOverlay", nullptr, kFlags))
            {
                if (info.mode == CameraControlMode::eFly)
                {
                    drawHintRow(ICON_MDI_MOUSE_RIGHT_CLICK " " ICON_MDI_EYE_OUTLINE, "Look");
                    drawHintRow(ICON_MDI_ALPHA_W_BOX " " ICON_MDI_ALPHA_A_BOX " " ICON_MDI_ALPHA_S_BOX " " ICON_MDI_ALPHA_D_BOX,
                                "Move");
                    drawHintRow(ICON_MDI_ALPHA_Q_BOX " " ICON_MDI_CHEVRON_DOWN_BOX "   " ICON_MDI_ALPHA_E_BOX " " ICON_MDI_CHEVRON_UP_BOX,
                                "Down / Up");
                    drawHintRow(ICON_MDI_APPLE_KEYBOARD_SHIFT " " ICON_MDI_RUN_FAST, "Faster");
                    drawHintRow(ICON_MDI_APPLE_KEYBOARD_CONTROL " " ICON_MDI_TURTLE, "Slower");
                }
                else
                {
                    drawHintRow(ICON_MDI_MOUSE_LEFT_CLICK " " ICON_MDI_ROTATE_ORBIT, "Rotate");
                    drawHintRow(ICON_MDI_MOUSE_SCROLL_WHEEL " " ICON_MDI_PAN, "Pan");
                    drawHintRow(ICON_MDI_MOUSE_SCROLL_WHEEL " " ICON_MDI_MAGNIFY, "Zoom");
                    drawHintRow(ICON_MDI_MOUSE_RIGHT_CLICK " " ICON_MDI_EYE_OUTLINE, "Fly");
                }
            }
            ImGui::End();
        }

        void drawXrMirrorControls(bool&  fitToPanel,
                                  float& manualScale,
                                  bool&  swapEyes,
                                  bool&  singleEye,
                                  int&   eyeIndex,
                                  int    maxEyeIndex)
        {
            if (ImGui::Button("Fit"))
                fitToPanel = true;

            ImGui::SameLine();
            if (ImGui::Button("1:1"))
            {
                fitToPanel  = false;
                manualScale = 1.0f;
            }

            ImGui::SameLine();
            ImGui::Checkbox("Swap Eyes", &swapEyes);

            ImGui::SameLine();
            ImGui::Checkbox("Single Eye", &singleEye);

            if (!fitToPanel)
                ImGui::SliderFloat("Scale", &manualScale, 0.1f, 2.0f, "%.2fx");

            if (singleEye)
                ImGui::SliderInt("Eye", &eyeIndex, 0, std::max(0, maxEyeIndex));
        }

        void syncTextureViewerRegistration(IImGuiService&                       imguiService,
                                           const resource::GpuResourcePool&     pool,
                                           std::vector<const rhi::Texture*>&    registeredTextures,
                                           std::vector<IImGuiService::TextureID>& textureIds)
        {
            uint32_t maxBindlessIndex = 0u;
            for (const auto& gpuTexture : pool.textures)
                maxBindlessIndex = std::max(maxBindlessIndex, gpuTexture.bindlessIndex);

            const size_t requiredSize = static_cast<size_t>(maxBindlessIndex) + 1u;
            if (registeredTextures.size() < requiredSize)
                registeredTextures.resize(requiredSize, nullptr);
            if (textureIds.size() < requiredSize)
                textureIds.resize(requiredSize, 0);

            std::vector<bool> alive(requiredSize, false);
            for (const auto& gpuTexture : pool.textures)
            {
                const auto index = static_cast<size_t>(gpuTexture.bindlessIndex);
                alive[index]     = true;
                syncImGuiTextureRegistration(
                    imguiService, gpuTexture.texture.get(), registeredTextures[index], textureIds[index]);
            }

            for (size_t i = 0; i < registeredTextures.size(); ++i)
            {
                const bool isAlive = i < alive.size() ? alive[i] : false;
                if (!isAlive && textureIds[i])
                {
                    imguiService.removeTexture(textureIds[i]);
                    registeredTextures[i] = nullptr;
                }
            }
        }

        void drawTextureViewer(const resource::GpuResourcePool&            pool,
                               std::vector<const rhi::Texture*>&           registeredTextures,
                               std::vector<IImGuiService::TextureID>&      textureIds,
                               int&                                         columns)
        {
            if (!ImGui::CollapsingHeader("Texture Viewer", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            ImGui::Text("Loaded GPU textures: %zu", pool.textures.size());
            ImGui::SliderInt("Columns", &columns, 1, 8);

            if (!ImGui::BeginTable("##TextureViewerTable", columns, ImGuiTableFlags_SizingStretchSame))
                return;

            for (size_t bindless = 0; bindless < registeredTextures.size(); ++bindless)
            {
                const auto* texture = registeredTextures[bindless];
                const auto  texId   = bindless < textureIds.size() ? textureIds[bindless] : 0;
                if (!texture || !texId)
                    continue;

                ImGui::TableNextColumn();
                ImGui::BeginGroup();
                ImGui::Image(texId, ImVec2(96.0f, 96.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                const auto extent = texture->getExtent();
                const auto format = rhi::toString(texture->getPixelFormat());
                ImGui::Text("Slot: %zu", bindless);
                ImGui::Text("Size: %ux%u", extent.width, extent.height);
                ImGui::Text("Mips: %u", texture->getNumMipLevels());
                ImGui::Text("Fmt : %.*s", static_cast<int>(format.size()), format.data());
                ImGui::EndGroup();
            }

            ImGui::EndTable();
        }
    } // namespace

    void UniversalRenderer::init()
    {
        auto* services = getServices();
        if (!services)
            return;

        const auto backendApi = services->require<IRenderBackendService>().renderDevice().getBackendApi();
#if defined(__ANDROID__)
        constexpr bool kForceCompatibilityFeature = true;
#else
        constexpr bool kForceCompatibilityFeature = false;
#endif
        const bool useCompatibilityFeature = kForceCompatibilityFeature || backendApi == rhi::RenderBackendApi::eWebGPU;
        if (useCompatibilityFeature)
        {
            m_GaussianSplatFeature = nullptr;
            emplaceFeature<CompatibilityFeature>();
            if (backendApi != rhi::RenderBackendApi::eWebGPU)
                emplaceFeature<FinalCompositionFeature>();
            return;
        }

        // Add features in the desired order.
        emplaceFeature<MeshletFeature>();
        m_GaussianSplatFeature = &emplaceFeature<GaussianSplatFeature>();
        emplaceFeature<TestFeature>();
        emplaceFeature<FinalCompositionFeature>();
    }

    void UniversalRenderer::onImGui()
    {
        drawFpsOverlay();

        auto* services       = getServices();
        auto& backendService = services->require<IRenderBackendService>();
        auto& cameraService  = services->require<ICameraService>();
        auto& imguiService   = services->require<IImGuiService>();
        auto& gpuResourceSvc = services->require<IGpuResourceService>();

        const bool suppressCameraInput = ImGui::GetIO().WantCaptureMouse || ImGui::IsAnyItemHovered() ||
                                         ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        cameraService.setCameraControlInputSuppressed(suppressCameraInput);

        drawCameraHintOverlay(cameraService.cameraControlOverlayInfo());

        ImGui::Begin("Universal Renderer");

        if (m_GaussianSplatFeature && ImGui::CollapsingHeader("3DGS Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& settings = m_GaussianSplatFeature->settings();
            const bool xrEnabled = backendService.isXREnabled();
            ImGui::SliderFloat("Frustum Dilation", &settings.frustumDilation, 1.0f, 1.5f, "%.2f");
            ImGui::SliderFloat("Alpha Cull Threshold", &settings.alphaCullThreshold, 0.0f, 0.02f, "%.5f");
            ImGui::SliderFloat("Size Culling Min Pixels", &settings.sizeCullingMinPixels, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Splat Scale", &settings.splatScale, 0.25f, 2.5f, "%.2f");
            ImGui::SliderFloat("Max Axis Pixels", &settings.maxAxisPixels, 64.0f, 1024.0f, "%.0f");
            ImGui::SliderFloat("Depth Iso Threshold", &settings.depthIsoThreshold, 0.1f, 0.99f, "%.2f");
            ImGui::Checkbox("Enable Exact Depth/Transmittance", &settings.enableExactDepthTransmittance);
            if (xrEnabled)
            {
                ImGui::Checkbox("Reuse XR Left-Eye Cull/Sort", &settings.enableXrViewReuse);
                ImGui::Checkbox("Enable XR Multiview", &settings.enableXrMultiview);
            }
        }

        if (backendService.isXREnabled() && backendService.isXRMirrorEnabled())
        {
            static bool  fitToPanel     = true;
            static float manualScale    = 1.0f;
            static bool  swapEyes       = false;
            static bool  singleEye      = false;
            static int   singleEyeIndex = 0;

            if (ImGui::CollapsingHeader("XR Mirror", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto eyeViews    = backendService.xrEyeViews();
                const int  maxEyeIndex = static_cast<int>(eyeViews.empty() ? 0u : (eyeViews.size() - 1u));
                drawXrMirrorControls(fitToPanel, manualScale, swapEyes, singleEye, singleEyeIndex, maxEyeIndex);

                const size_t mirrorCount = std::min<std::size_t>(eyeViews.size(), m_XRMirrorTextureIds.size());
                for (size_t eyeIndex = 0; eyeIndex < mirrorCount; ++eyeIndex)
                {
                    const auto& eyeView = eyeViews[eyeIndex];
                    if (!eyeView.mirrorTarget)
                        continue;

                    syncImGuiTextureRegistration(imguiService,
                                                 eyeView.mirrorTarget,
                                                 m_XRMirrorTextures[eyeIndex],
                                                 m_XRMirrorTextureIds[eyeIndex]);
                }

                const bool   drawSingleEye = singleEye || mirrorCount <= 1u;
                const size_t primaryEye    = swapEyes && mirrorCount > 1u ? 1u : 0u;
                const size_t secondaryEye  = swapEyes && mirrorCount > 1u ? 0u : 1u;

                auto drawEye = [&](size_t eyeIndex, float slotWidth) {
                    if (eyeIndex >= mirrorCount)
                        return;

                    const auto& eyeView   = eyeViews[eyeIndex];
                    const auto  textureId = m_XRMirrorTextureIds[eyeIndex];
                    if (!eyeView.mirrorTarget || !textureId)
                        return;

                    const auto   extent       = eyeView.mirrorTarget->getExtent();
                    const float  nativeWidth  = static_cast<float>(std::max(extent.width, 1u));
                    const float  nativeHeight = static_cast<float>(std::max(extent.height, 1u));
                    const float  aspect       = nativeHeight / nativeWidth;
                    const float  drawWidth    = fitToPanel ? slotWidth : std::min(slotWidth, nativeWidth * manualScale);
                    const ImVec2 imageSize {drawWidth, drawWidth * aspect};

                    ImGui::BeginGroup();
                    ImGui::Text("Eye %u  %ux%u", eyeView.eyeIndex, extent.width, extent.height);
                    ImGui::Image(textureId, imageSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                    ImGui::EndGroup();
                };

                const float spacing    = ImGui::GetStyle().ItemSpacing.x;
                const float availWidth = ImGui::GetContentRegionAvail().x;

                if (drawSingleEye)
                {
                    const size_t eyeIndex = static_cast<size_t>(
                        std::clamp(singleEyeIndex, 0, static_cast<int>(mirrorCount > 0 ? mirrorCount - 1u : 0u)));
                    drawEye(eyeIndex, std::max(1.0f, availWidth));
                }
                else
                {
                    const float slotWidth = std::max(1.0f, (availWidth - spacing) * 0.5f);
                    drawEye(primaryEye, slotWidth);
                    ImGui::SameLine();
                    drawEye(secondaryEye, slotWidth);
                }
            }
        }
        else
        {
            for (auto& textureId : m_XRMirrorTextureIds)
            {
                if (textureId)
                    imguiService.removeTexture(textureId);
            }
            m_XRMirrorTextures.fill(nullptr);
        }

        syncTextureViewerRegistration(
            imguiService, gpuResourceSvc.pool(), m_TextureViewerRegisteredTextures, m_TextureViewerTextureIds);
        drawTextureViewer(
            gpuResourceSvc.pool(), m_TextureViewerRegisteredTextures, m_TextureViewerTextureIds, m_TextureViewerColumns);

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            getServices()->require<IFrameDebuggerService>().captureSingleFrame();
        }
#endif
        ImGui::End();
    }
} // namespace vultra
