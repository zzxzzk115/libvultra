#include "editor_app/ui/windows/game_view_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <limits>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t kRenderTargetReleaseDelayFrames = 3;

        glm::mat4 makeTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 makeWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto local = makeTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;

            return makeWorldTransformMatrix(reg, hierarchy->parent) * local;
        }

        glm::mat4 makeProjection(const vultra::CameraComponent& camera, const float aspect)
        {
            const float zNear = std::max(camera.zNear, 0.0001f);
            const float zFar  = std::max(camera.zFar, zNear + 0.0001f);
            if (camera.projection == 1u)
            {
                const float height = std::max(camera.orthographicHeight, 0.0001f);
                const float width  = height * std::max(aspect, 0.0001f);
                return glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, zNear, zFar);
            }

            return glm::perspectiveRH_ZO(
                glm::radians(camera.fovYDegrees), std::max(aspect, 0.0001f), zNear, zFar);
        }

        entt::entity findPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent, vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best = entt::null;
            int          bestPriority = std::numeric_limits<int>::min();
            for (auto e : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(e);
                if (!camera.primary)
                    continue;
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = e;
                    bestPriority = camera.priority;
                }
            }
            return best;
        }

        vultra::RenderCamera makeGameCamera(vultra::World&        world,
                                            const entt::entity    entity,
                                            const float           aspect,
                                            vultra::rhi::Texture* target)
        {
            auto& reg       = world.registry();
            auto& id        = reg.get<vultra::IDComponent>(entity);
            auto& camera    = reg.get<vultra::CameraComponent>(entity);

            vultra::RenderCamera out {};
            out.uuid        = id.uuid;
            out.name        = reg.all_of<vultra::NameComponent>(entity) ? reg.get<vultra::NameComponent>(entity).name : "Game Camera";
            out.priority    = camera.priority;
            out.view        = glm::inverse(makeWorldTransformMatrix(reg, entity));
            out.projection  = makeProjection(camera, aspect);
            out.zNear       = std::max(camera.zNear, 0.0001f);
            out.zFar        = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY        = glm::radians(camera.fovYDegrees);
            out.target      = target;
            out.clearValue  = camera.clearColor;
            out.clearMode   = camera.clearMode;
            out.renderImGui = false;
            out.debugEntityIdOutput = false;
            out.selectionOutlineEnabled = false;
            out.rendererKey = camera.rendererKey.empty() ? "universal" : camera.rendererKey;
            return out;
        }

        void createDefaultCamera(vultra::World& world)
        {
            auto& reg = world.registry();
            auto  e   = world.createEntity();
            reg.emplace<vultra::NameComponent>(e, vultra::NameComponent {"Camera"});
            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(e);
            transform.position = {0.0f, 1.6f, 4.0f};
            transform.rotation = glm::quat(glm::radians(glm::vec3 {-12.0f, 180.0f, 0.0f}));
            transform.dirty    = true;
            reg.emplace<vultra::CameraComponent>(e, vultra::CameraComponent {.primary = true});
            if (auto* id = reg.try_get<vultra::IDComponent>(e))
                Selection::select(SelectionCategory::Entity, id->uuid);
        }

        std::string formatBytes(const uint64_t bytes)
        {
            constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
            double                value    = static_cast<double>(bytes);
            size_t                unit     = 0;
            while (value >= 1024.0 && unit + 1 < (sizeof(kUnits) / sizeof(kUnits[0])))
            {
                value /= 1024.0;
                ++unit;
            }

            char buffer[64] {};
            if (unit == 0)
                std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, kUnits[unit]);
            else
                std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
            return buffer;
        }
    } // namespace

    GameViewWindow::GameViewWindow() : EditorWindow("Game View", ICON_MDI_GAMEPAD_VARIANT) {}

    void GameViewWindow::onClosed(EditorContext& ctx) { releaseRenderTarget(ctx); }

    void GameViewWindow::onDestroy(EditorContext& ctx) { releaseRenderTarget(ctx); }

    void GameViewWindow::draw(EditorContext& ctx)
    {
        resetRenderTargetsForProject(ctx);

        const bool visible =
            ImGui::Begin(title().c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ctx.state.gameViewVisible = visible && !ImGui::IsWindowCollapsed();
        if (!visible)
        {
            ImGui::End();
            return;
        }
        drawToolbar(ctx);

        ImGui::BeginChild("##GameViewport", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x      = std::max(1.0f, avail.x);
        avail.y      = std::max(1.0f, avail.y);
        m_LastViewportAvail = avail;

        const ImVec2 outputSize = computeRenderSize(avail);
        ctx.state.gameViewRenderWidth =
            static_cast<uint32_t>(std::max(outputSize.x, 1.0f));
        ctx.state.gameViewRenderHeight =
            static_cast<uint32_t>(std::max(outputSize.y, 1.0f));
        ensureRenderTarget(ctx, static_cast<uint32_t>(outputSize.x), static_cast<uint32_t>(outputSize.y));

        m_MinZoom = m_SelectedResolution == 0 ? 1.0f : computeFitZoom(avail, outputSize);
        m_UserZoom = std::clamp(m_UserZoom, m_MinZoom, 4.0f);
        const float zoom = m_SelectedResolution == 0 ? 1.0f : m_UserZoom;
        const ImVec2 displaySize {outputSize.x * zoom, outputSize.y * zoom};
        ImVec2 cursor = ImGui::GetCursorPos();
        if (displaySize.x < avail.x)
            cursor.x += (avail.x - displaySize.x) * 0.5f;
        if (displaySize.y < avail.y)
            cursor.y += (avail.y - displaySize.y) * 0.5f;
        ImGui::SetCursorPos(cursor);

        if (m_ActiveRenderTarget.textureId)
            ImGui::Image(m_ActiveRenderTarget.textureId, displaySize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        else
            ImGui::InvisibleButton("##GameViewCanvas", displaySize);

        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        auto*      dl  = ImGui::GetWindowDrawList();
        dl->AddRect(min, max, IM_COL32(70, 78, 90, 255));

        bool hasPrimaryCamera = false;
        if (ctx.services)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
            {
                auto& world = worldService->world();
                auto  cam   = findPrimaryCamera(world);
                hasPrimaryCamera = cam != entt::null;

                auto* renderTarget =
                    m_PendingRenderTarget.texture ? &*m_PendingRenderTarget.texture :
                                                    (m_ActiveRenderTarget.texture ? &*m_ActiveRenderTarget.texture :
                                                                                    nullptr);
                if (hasPrimaryCamera && renderTarget != nullptr)
                {
                    const float aspect = outputSize.x / std::max(outputSize.y, 1.0f);
                    auto        renderCamera = makeGameCamera(world, cam, aspect, renderTarget);
                    if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                        cameraService->addManualCamera(renderCamera);
                }
            }
        }

        if (!hasPrimaryCamera)
        {
            dl->AddRectFilled(min, max, IM_COL32(15, 17, 21, 255));
            ImGui::SetCursorScreenPos(min);
            ui::emptyState(ICON_MDI_CAMERA_OFF_OUTLINE,
                           "No Primary Camera",
                           "Create or mark a CameraComponent as primary to preview the game.");

            const ImVec2 panelCenter {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
            ImGui::SetCursorScreenPos(ImVec2(panelCenter.x - 74.0f, panelCenter.y));
            if (ImGui::Button(ICON_MDI_CAMERA_PLUS "  Create Camera", ImVec2(174.0f, 0.0f)) && ctx.services)
            {
                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                {
                    createDefaultCamera(worldService->world());
                    ctx.state.sceneDirty = true;
                    ctx.state.statusMessage = "Created a primary Camera entity.";
                }
            }
        }

        drawMetricsOverlay(ctx, min, max);

        ImGui::EndChild();
        ImGui::End();
    }

    void GameViewWindow::drawMetricsOverlay(EditorContext& ctx, const ImVec2& imageMin, const ImVec2& imageMax)
    {
        auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        auto* profiler      = renderService ? renderService->runtimeProfiler() : nullptr;

        if (!ctx.state.metricsOverlayVisible)
            return;

        if (profiler && !profiler->isEnabled())
            profiler->setEnabled(true);

        const ImGuiIO& io      = ImGui::GetIO();
        const float    fps     = io.Framerate;
        const float    frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

        ImGui::SetNextWindowPos(ImVec2 {imageMax.x - 10.0f, imageMin.y + 10.0f}, ImGuiCond_Always, ImVec2 {1.0f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {12.0f, 10.0f});

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##GameViewMetricsOverlay", nullptr, flags))
        {
            ImGui::TextUnformatted(ICON_MDI_CHART_LINE " Metrics");
            ImGui::Separator();
            ImGui::Text("FPS        %.1f", fps);
            ImGui::Text("Frame      %.2f ms", frameMs);

            const auto* selectedFrame = profiler ? profiler->selectedFrame() : nullptr;
            if (selectedFrame)
            {
                ImGui::Text("CPU frame  %.2f ms", selectedFrame->cpuFrameMs);
                if (selectedFrame->gpuFrameMs >= 0.0)
                    ImGui::Text("GPU frame  %.2f ms", selectedFrame->gpuFrameMs);
                ImGui::Separator();
                ImGui::Text("Draws      %llu", static_cast<unsigned long long>(selectedFrame->drawCalls));
                ImGui::Text("Dispatch   %llu", static_cast<unsigned long long>(selectedFrame->dispatchCalls));
                ImGui::Text("VRAM local %s", formatBytes(selectedFrame->gpuDeviceLocalBytes).c_str());
            }
            else
            {
                ImGui::TextDisabled("Profiler warming up...");
            }

            if (renderService)
            {
                const auto& gaussianStats = renderService->gaussianSplatFrameStats();
                if (gaussianStats.splatAssets > 0u || gaussianStats.totalSplats > 0u)
                {
                    ImGui::Separator();
                    ImGui::Text("Splats     %u / %u", gaussianStats.preparedSplats, gaussianStats.totalSplats);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void GameViewWindow::drawToolbar(EditorContext& ctx)
    {
        (void)ctx;

        const char* resolutionLabels[] = {"Free Aspect", "16:9", "4:3", "21:9", "1920x1080", "1280x720", "800x600"};
        ImGui::SetNextItemWidth(126.0f);
        if (ImGui::Combo("##GameViewResolution", &m_SelectedResolution, resolutionLabels, IM_ARRAYSIZE(resolutionLabels)))
        {
            const auto renderSize = computeRenderSize(m_LastViewportAvail);
            m_MinZoom = m_SelectedResolution == 0 ? 1.0f : computeFitZoom(m_LastViewportAvail, renderSize);
            m_UserZoom = m_MinZoom;
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(ICON_MDI_MAGNIFY " Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (m_SelectedResolution == 0)
            ImGui::BeginDisabled();
        ImGui::SliderFloat("##GameViewZoom", &m_UserZoom, m_MinZoom, 4.0f, "%.2fx");
        if (m_SelectedResolution == 0)
            ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 14.0f);
        const ImVec2 target = m_ActiveRenderTarget.texture ? ImVec2(static_cast<float>(m_ActiveRenderTarget.extent.width),
                                                                    static_cast<float>(m_ActiveRenderTarget.extent.height))
                                                           : ImVec2(0.0f, 0.0f);
        ImGui::TextDisabled("Res: %dx%d", static_cast<int>(target.x), static_cast<int>(target.y));
    }

    ImVec2 GameViewWindow::computeRenderSize(const ImVec2& avail) const
    {
        auto fitAspect = [&](const float aspect)
        {
            const float heightFromWidth = avail.x / aspect;
            if (heightFromWidth <= avail.y)
                return ImVec2(std::max(1.0f, avail.x), std::max(1.0f, heightFromWidth));
            return ImVec2(std::max(1.0f, avail.y * aspect), std::max(1.0f, avail.y));
        };

        switch (m_SelectedResolution)
        {
        case 1:
            return fitAspect(16.0f / 9.0f);
        case 2:
            return fitAspect(4.0f / 3.0f);
        case 3:
            return fitAspect(21.0f / 9.0f);
        case 4:
            return {1920.0f, 1080.0f};
        case 5:
            return {1280.0f, 720.0f};
        case 6:
            return {800.0f, 600.0f};
        default:
            return {std::max(1.0f, avail.x), std::max(1.0f, avail.y)};
        }
    }

    float GameViewWindow::computeFitZoom(const ImVec2& avail, const ImVec2& renderSize) const
    {
        if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
            return 1.0f;
        return std::min(avail.x / renderSize.x, avail.y / renderSize.y);
    }

    void GameViewWindow::ensureRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        collectRetiredRenderTargets(ctx);
        if (m_PendingRenderTarget.texture &&
            static_cast<uint64_t>(ImGui::GetFrameCount()) > m_PendingRenderTarget.frameCreated)
        {
            promotePendingRenderTarget(ctx);
        }

        const auto& currentTarget =
            m_PendingRenderTarget.texture ? m_PendingRenderTarget : m_ActiveRenderTarget;
        if (currentTarget.texture && currentTarget.extent.width == width && currentTarget.extent.height == height &&
            currentTarget.textureId)
            return;

        if (m_PendingRenderTarget.texture)
            retireRenderTarget(m_PendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_PendingRenderTarget.extent = {width, height};
        m_PendingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_PendingRenderTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                .build(rd);
        m_PendingRenderTarget.textureId     = imguiService->addTexture(*m_PendingRenderTarget.texture);
        m_PendingRenderTarget.frameCreated  = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_PendingRenderTarget.releaseFrame  = 0;
    }

    void GameViewWindow::promotePendingRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_PendingRenderTarget.texture)
            return;

        retireRenderTarget(m_ActiveRenderTarget);
        m_ActiveRenderTarget  = std::move(m_PendingRenderTarget);
        m_PendingRenderTarget = {};
    }

    void GameViewWindow::retireRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture && !slot.textureId)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_RetiredRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void GameViewWindow::collectRetiredRenderTargets(EditorContext& ctx)
    {
        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

        std::size_t out = 0;
        for (auto& slot : m_RetiredRenderTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imguiService && slot.textureId)
                    imguiService->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            else
            {
                m_RetiredRenderTargets[out++] = std::move(slot);
            }
        }
        m_RetiredRenderTargets.resize(out);
    }

    void GameViewWindow::releaseRenderTarget(EditorContext& ctx)
    {
        if (ctx.services)
        {
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                if (m_ActiveRenderTarget.textureId)
                    imguiService->removeTexture(m_ActiveRenderTarget.textureId);
                if (m_PendingRenderTarget.textureId)
                    imguiService->removeTexture(m_PendingRenderTarget.textureId);
                for (auto& slot : m_RetiredRenderTargets)
                {
                    if (slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                }
            }
        }
        m_ActiveRenderTarget  = {};
        m_PendingRenderTarget = {};
        m_RetiredRenderTargets.clear();
    }

    void GameViewWindow::resetRenderTargetsForProject(EditorContext& ctx)
    {
        (void)ctx;
        if (m_ProjectGeneration == ctx.state.projectGeneration)
            return;

        retireRenderTarget(m_ActiveRenderTarget);
        retireRenderTarget(m_PendingRenderTarget);
        m_ProjectGeneration = ctx.state.projectGeneration;
    }
} // namespace vultra_app
