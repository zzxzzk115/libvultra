#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#ifdef VULTRA_ENABLE_RENDERDOC
#include "vultra/function/services/frame_debugger_service.hpp"
#endif

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace vultra
{
    namespace
    {
        uint64_t getTextureHandleId(const rhi::Texture& texture)
        {
            return static_cast<uint64_t>(static_cast<VkImage>(texture.getImageHandle()));
        }

        void syncImGuiTextureRegistration(IImGuiService&            imguiService,
                                          const rhi::Texture*       texture,
                                          const uint64_t            textureHandleId,
                                          const rhi::Texture*&      registeredTexture,
                                          uint64_t&                 registeredTextureHandleId,
                                          IImGuiService::TextureID& textureId)
        {
            const bool sameTexture = registeredTexture == texture && registeredTextureHandleId == textureHandleId;
            if (sameTexture)
                return;

            if (textureId)
                imguiService.removeTexture(textureId);

            registeredTexture         = texture;
            registeredTextureHandleId = textureHandleId;
            textureId                 = texture ? imguiService.addTexture(*texture) : 0;
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
    } // namespace

    void UniversalRenderer::init()
    {
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
        auto& imguiService   = services->require<IImGuiService>();

        ImGui::Begin("Universal Renderer");

        if (m_GaussianSplatFeature && ImGui::CollapsingHeader("3DGS Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& settings = m_GaussianSplatFeature->settings();
            ImGui::SliderFloat("Frustum Dilation", &settings.frustumDilation, 1.0f, 1.5f, "%.2f");
            ImGui::SliderFloat("Alpha Cull Threshold", &settings.alphaCullThreshold, 0.0f, 0.02f, "%.5f");
            ImGui::SliderFloat("Size Culling Min Pixels", &settings.sizeCullingMinPixels, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Splat Scale", &settings.splatScale, 0.25f, 2.5f, "%.2f");
            ImGui::SliderFloat("Max Axis Pixels", &settings.maxAxisPixels, 64.0f, 1024.0f, "%.0f");
            ImGui::SliderFloat("Depth Iso Threshold", &settings.depthIsoThreshold, 0.1f, 0.99f, "%.2f");
            ImGui::Checkbox("Enable Exact Depth/Transmittance", &settings.enableExactDepthTransmittance);
            ImGui::Checkbox("Reuse XR Left-Eye Cull/Sort", &settings.enableXrViewReuse);
            ImGui::Checkbox("Enable XR Multiview", &settings.enableXrMultiview);
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
                                                 getTextureHandleId(*eyeView.mirrorTarget),
                                                 m_XRMirrorTextures[eyeIndex],
                                                 m_XRMirrorImageHandles[eyeIndex],
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
            m_XRMirrorImageHandles.fill(0u);
        }

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
