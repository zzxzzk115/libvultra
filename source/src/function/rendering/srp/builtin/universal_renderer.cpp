#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/rendering/srp/builtin/features/compatibility_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#ifdef VULTRA_ENABLE_RENDERDOC
#include "vultra/function/services/frame_debugger_service.hpp"
#endif

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <implot/implot.h>

#include <algorithm>
#include <functional>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string formatBytes(const uint64_t bytes)
        {
            constexpr double kKB = 1024.0;
            constexpr double kMB = 1024.0 * 1024.0;
            constexpr double kGB = 1024.0 * 1024.0 * 1024.0;

            const double v = static_cast<double>(bytes);
            char         buf[64] {};
            if (v >= kGB)
            {
                std::snprintf(buf, sizeof(buf), "%.2f GB", v / kGB);
            }
            else if (v >= kMB)
            {
                std::snprintf(buf, sizeof(buf), "%.2f MB", v / kMB);
            }
            else if (v >= kKB)
            {
                std::snprintf(buf, sizeof(buf), "%.2f KB", v / kKB);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
            }
            return std::string(buf);
        }

        void drawRuntimeProfilerPanel(IRenderService& renderService)
        {
            auto* profiler = renderService.runtimeProfiler();
            if (!profiler)
                return;

            if (!ImGui::CollapsingHeader("Built-in Profiler", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            bool enabled = profiler->isEnabled();
            if (ImGui::Checkbox("Enable Internal Profiler", &enabled))
                profiler->setEnabled(enabled);

            if (!enabled)
                return;

            bool paused = profiler->isPaused();
            if (ImGui::Checkbox("Pause", &paused))
                profiler->setPaused(paused);

            const char* sortLabels[] = {"Total (ms)", "Self (ms)", "Calls", "Name"};
            int         sortIndex    = static_cast<int>(profiler->getSortKey());
            if (ImGui::Combo("Sort", &sortIndex, sortLabels, IM_ARRAYSIZE(sortLabels)))
                profiler->setSortKey(static_cast<RuntimeProfiler::SortKey>(sortIndex));

            if (paused && profiler->historySize() > 0)
            {
                int frozen = profiler->getFrozenHistoryIndex();
                if (frozen < 0)
                    frozen = static_cast<int>(profiler->historySize()) - 1;
                if (ImGui::SliderInt("Frozen Frame", &frozen, 0, static_cast<int>(profiler->historySize()) - 1))
                    profiler->setFrozenHistoryIndex(frozen);
            }

            const auto* selected = profiler->selectedFrame();
            if (!selected)
            {
                ImGui::TextUnformatted("No profiler frames yet.");
                return;
            }

            ImGui::SeparatorText("Frame Summary");
            ImGui::Text("Frame: %llu", static_cast<unsigned long long>(selected->frameIndex));
            ImGui::Text("CPU Frame: %.3f ms", selected->cpuFrameMs);
            ImGui::Text("CPU Render: %.3f ms", selected->cpuRenderMs);
            if (selected->gpuFrameMs >= 0.0)
                ImGui::Text("GPU Frame: %.3f ms", selected->gpuFrameMs);
            else
                ImGui::TextUnformatted("GPU Frame: N/A");

            ImGui::Text("Draw Calls: %llu", static_cast<unsigned long long>(selected->drawCalls));
            ImGui::Text("Dispatch: %llu", static_cast<unsigned long long>(selected->dispatchCalls));
            ImGui::Text("Trace Rays: %llu", static_cast<unsigned long long>(selected->traceRaysCalls));
            ImGui::Text("Copy Ops: %llu", static_cast<unsigned long long>(selected->copyOps));
            ImGui::Text("Update Ops: %llu", static_cast<unsigned long long>(selected->updateOps));
            ImGui::Text("VSync: %s", selected->vsyncEnabled ? "On" : "Off");
            ImGui::Text("Asset CPU Cache: %s", formatBytes(selected->assetCpuCacheBytes).c_str());
            ImGui::Text("Render CPU Cache: %s", formatBytes(selected->renderCpuCacheBytes).c_str());
            ImGui::Text("GPU Device Local: %s", formatBytes(selected->gpuDeviceLocalBytes).c_str());
            ImGui::Text("GPU Host Visible: %s", formatBytes(selected->gpuHostVisibleBytes).c_str());
            ImGui::Text("GPU Scope Begin/Token/Resolved: %u / %u / %u",
                        selected->gpuScopeBeginCount,
                        selected->gpuScopeTokenCount,
                        selected->gpuScopeResolvedCount);

            const auto& history = profiler->history();
            if (!history.empty() && ImPlot::BeginPlot("Frame Times", ImVec2(-1, 180)))
            {
                static std::vector<double> x;
                static std::vector<double> cpu;
                static std::vector<double> gpu;
                x.resize(history.size());
                cpu.resize(history.size());
                gpu.resize(history.size());

                for (size_t i = 0; i < history.size(); ++i)
                {
                    x[i]   = static_cast<double>(i);
                    cpu[i] = history[i].cpuFrameMs;
                    gpu[i] = history[i].gpuFrameMs >= 0.0 ? history[i].gpuFrameMs : 0.0;
                }

                ImPlot::SetupAxes("Frame", "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("CPU", x.data(), cpu.data(), static_cast<int>(cpu.size()));
                if (std::any_of(history.begin(), history.end(), [](const auto& f) { return f.gpuFrameMs >= 0.0; }))
                    ImPlot::PlotLine("GPU", x.data(), gpu.data(), static_cast<int>(gpu.size()));
                ImPlot::EndPlot();
            }

            ImGui::SeparatorText("Scope Trees");

            auto drawScopeTreeTable = [&](const char*                                      title,
                                          const char*                                      tableId,
                                          const std::vector<RuntimeProfiler::ScopeNode>&   nodes,
                                          const bool                                       gpuTree) {
                ImGui::TextUnformatted(title);
                if (nodes.empty())
                {
                    ImGui::TextUnformatted("No scope data.");
                    return;
                }

                if (!ImGui::BeginTable(tableId,
                                       4,
                                       ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                       ImVec2(-1.0f, 180.0f)))
                    return;

                ImGui::TableSetupColumn("Scope");
                ImGui::TableSetupColumn(gpuTree ? "GPU Total (ms)" : "CPU Total (ms)", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn(gpuTree ? "GPU Self (ms)" : "CPU Self (ms)", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();

                std::vector<std::vector<int>> children(nodes.size());
                for (size_t i = 1; i < nodes.size(); ++i)
                {
                    const int parent = nodes[i].parent;
                    if (parent >= 0 && static_cast<size_t>(parent) < children.size())
                        children[static_cast<size_t>(parent)].push_back(static_cast<int>(i));
                }

                auto sortChildren = [&](std::vector<int>& list) {
                    const auto sortKey = profiler->getSortKey();
                    std::stable_sort(list.begin(), list.end(), [&](const int ia, const int ib) {
                        const auto& a = nodes[static_cast<size_t>(ia)];
                        const auto& b = nodes[static_cast<size_t>(ib)];
                        switch (sortKey)
                        {
                            case RuntimeProfiler::SortKey::eTotalMs:
                                return gpuTree ? a.gpuTotalMs > b.gpuTotalMs : a.totalMs > b.totalMs;
                            case RuntimeProfiler::SortKey::eSelfMs:
                                return gpuTree ? a.gpuSelfMs > b.gpuSelfMs : a.selfMs > b.selfMs;
                            case RuntimeProfiler::SortKey::eCalls:
                                return a.callCount > b.callCount;
                            case RuntimeProfiler::SortKey::eName:
                                return a.name < b.name;
                        }
                        return a.totalMs > b.totalMs;
                    });
                };

                std::function<void(int)> drawNode = [&](const int idx) {
                    const auto& node      = nodes[static_cast<size_t>(idx)];
                    auto&       childList = children[static_cast<size_t>(idx)];
                    sortChildren(childList);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    const bool isLeaf = childList.empty();
                    ImGuiTreeNodeFlags flags = isLeaf ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen) : 0;
                    const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(idx)),
                                                          flags,
                                                          "%s",
                                                          node.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    if (gpuTree)
                    {
                        if (node.gpuTotalMs >= 0.0)
                            ImGui::Text("%.3f", node.gpuTotalMs);
                        else
                            ImGui::TextUnformatted("N/A");
                    }
                    else
                    {
                        ImGui::Text("%.3f", node.totalMs);
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (gpuTree)
                    {
                        if (node.gpuSelfMs >= 0.0)
                            ImGui::Text("%.3f", node.gpuSelfMs);
                        else
                            ImGui::TextUnformatted("N/A");
                    }
                    else
                    {
                        ImGui::Text("%.3f", node.selfMs);
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", node.callCount);

                    if (!isLeaf && opened)
                    {
                        for (const int child : childList)
                            drawNode(child);
                        ImGui::TreePop();
                    }
                };

                drawNode(0);
                ImGui::EndTable();
            };

            drawScopeTreeTable("CPU Tree", "##RuntimeProfilerCpuTree", selected->cpuScopeTree, false);
            drawScopeTreeTable("GPU Tree", "##RuntimeProfilerGpuTree", selected->gpuScopeTree, true);
        }

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
                    imguiService,
                    gpuTexture.texture.get(),
                    registeredTextures[index],
                    textureIds[index]);
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
        const bool forceCompatibilityByCli = m_RenderPath == RenderPath::eCompatibility;
        const bool useCompatibilityFeature =
            kForceCompatibilityFeature || forceCompatibilityByCli || backendApi == rhi::RenderBackendApi::eWebGPU;
        if (useCompatibilityFeature)
        {
            m_GaussianSplatFeature = nullptr;
            emplaceFeature<CompatibilityFeature>();
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
        auto& renderService  = services->require<IRenderService>();
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

        drawRuntimeProfilerPanel(renderService);

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
