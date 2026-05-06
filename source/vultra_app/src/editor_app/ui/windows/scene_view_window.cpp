#include "editor_app/ui/windows/scene_view_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/core/services/input_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <ImGuizmo/ImGuizmo.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t kRenderTargetReleaseDelayFrames = 3;
        constexpr glm::vec3 kWorldUp {0.0f, 1.0f, 0.0f};

        glm::vec3 makeForward(const float yawDegrees, const float pitchDegrees)
        {
            const float yaw   = glm::radians(yawDegrees);
            const float pitch = glm::radians(pitchDegrees);
            return glm::normalize(glm::vec3 {
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch),
            });
        }

        entt::entity findEntityByUUID(vultra::World& world, const vultra::CoreUUID& uuid)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::IDComponent>(e).uuid == uuid)
                    return e;
            }
            return entt::null;
        }

        vultra::RenderCamera makeEditorCamera(const glm::vec3& position,
                                              const float      yaw,
                                              const float      pitch,
                                              const float      fovY,
                                              const float      aspect,
                                              vultra::rhi::Texture* target)
        {
            const auto forward = makeForward(yaw, pitch);

            vultra::RenderCamera camera {};
            camera.name        = "Scene View";
            camera.priority    = -100;
            camera.view        = glm::lookAt(position, position + forward, kWorldUp);
            camera.projection  = glm::perspectiveRH_ZO(glm::radians(fovY), std::max(aspect, 0.0001f), 0.05f, 2000.0f);
            camera.zNear       = 0.05f;
            camera.zFar        = 2000.0f;
            camera.fovY        = glm::radians(fovY);
            camera.target      = target;
            camera.clearValue  = {0.035f, 0.04f, 0.052f, 1.0f};
            camera.renderImGui = false;
            camera.rendererKey = "universal";
            return camera;
        }

        glm::mat4 makeTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        ImGuizmo::OPERATION toGizmoOperation(const SceneViewWindow::Tool tool)
        {
            switch (tool)
            {
                case SceneViewWindow::Tool::Move:
                    return ImGuizmo::TRANSLATE;
                case SceneViewWindow::Tool::Rotate:
                    return ImGuizmo::ROTATE;
                case SceneViewWindow::Tool::Scale:
                    return ImGuizmo::SCALE;
                case SceneViewWindow::Tool::Select:
                    return ImGuizmo::TRANSLATE;
            }
            return ImGuizmo::TRANSLATE;
        }
    } // namespace

    SceneViewWindow::SceneViewWindow() : EditorWindow("Scene View") {}

    void SceneViewWindow::onClosed(EditorContext& ctx) { releaseRenderTarget(ctx); }

    void SceneViewWindow::onDestroy(EditorContext& ctx) { releaseRenderTarget(ctx); }

    void SceneViewWindow::draw(EditorContext& ctx)
    {
        ImGui::Begin(m_Name.c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawToolbar();
        ImGui::Separator();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x      = std::max(1.0f, avail.x);
        avail.y      = std::max(1.0f, avail.y);

        ensureRenderTarget(ctx, static_cast<uint32_t>(avail.x), static_cast<uint32_t>(avail.y));

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if (m_ActiveRenderTarget.textureId)
            ImGui::Image(m_ActiveRenderTarget.textureId, avail, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        else
            ImGui::InvisibleButton("##SceneViewCanvas", avail);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const bool   hovered  = ImGui::IsItemHovered();
        auto*        dl       = ImGui::GetWindowDrawList();

        if (m_ShowGrid)
        {
            constexpr float step = 32.0f;
            for (float x = imageMin.x; x < imageMax.x; x += step)
                dl->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), IM_COL32(255, 255, 255, 18));
            for (float y = imageMin.y; y < imageMax.y; y += step)
                dl->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), IM_COL32(255, 255, 255, 18));
        }
        dl->AddRect(imageMin, imageMax, IM_COL32(90, 100, 118, 255));
        dl->AddText(ImVec2(imageMin.x + 12.0f, imageMin.y + 10.0f),
                    IM_COL32(225, 232, 242, 220),
                    ICON_MDI_MOUSE_RIGHT_CLICK " Fly   " ICON_MDI_MOUSE_SCROLL_WHEEL " Zoom   " ICON_MDI_APPLE_KEYBOARD_SHIFT " Fast");

        const float aspect = avail.x / std::max(avail.y, 1.0f);
        auto*       renderTarget =
            m_PendingRenderTarget.texture ? &*m_PendingRenderTarget.texture :
                                            (m_ActiveRenderTarget.texture ? &*m_ActiveRenderTarget.texture : nullptr);
        auto        editorCamera =
            makeEditorCamera(m_CameraPosition, m_CameraYaw, m_CameraPitch, m_CameraFovY, aspect, renderTarget);

        if (ctx.services)
        {
            if (auto* input = ctx.services->tryGet<vultra::IInputService>())
            {
                const bool flyActive = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
                if (flyActive)
                {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    m_CameraYaw += delta.x * 0.12f;
                    m_CameraPitch = std::clamp(m_CameraPitch - delta.y * 0.12f, -89.0f, 89.0f);

                    const auto forward = makeForward(m_CameraYaw, m_CameraPitch);
                    const auto right   = glm::normalize(glm::cross(forward, kWorldUp));
                    glm::vec3  move {};
                    if (input->getKey(vultra::KeyCode::eW))
                        move += forward;
                    if (input->getKey(vultra::KeyCode::eS))
                        move -= forward;
                    if (input->getKey(vultra::KeyCode::eD))
                        move += right;
                    if (input->getKey(vultra::KeyCode::eA))
                        move -= right;
                    if (input->getKey(vultra::KeyCode::eE))
                        move += kWorldUp;
                    if (input->getKey(vultra::KeyCode::eQ))
                        move -= kWorldUp;
                    if (glm::dot(move, move) > 0.0f)
                    {
                        const bool shift =
                            input->getKey(vultra::KeyCode::eLShift) || input->getKey(vultra::KeyCode::eRShift);
                        m_CameraPosition += glm::normalize(move) * (shift ? 0.28f : 0.08f);
                    }
                }

                if (hovered)
                {
                    const float wheel = input->getMouseScrollDelta().y;
                    if (std::abs(wheel) > 0.0f)
                        m_CameraPosition += makeForward(m_CameraYaw, m_CameraPitch) * (wheel * 0.45f);
                }
            }

            editorCamera =
                makeEditorCamera(m_CameraPosition, m_CameraYaw, m_CameraPitch, m_CameraFovY, aspect, renderTarget);

            if (renderTarget != nullptr)
            {
                if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                    cameraService->addManualCamera(editorCamera);
            }

            if (m_Tool != Tool::Select)
            {
                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                {
                    auto& world = worldService->world();
                    auto& reg   = world.registry();
                    auto  e     = findEntityByUUID(world, Selection::lastId());
                    if (e != entt::null && reg.valid(e) && reg.all_of<vultra::TransformComponent>(e))
                    {
                        auto& transform = reg.get<vultra::TransformComponent>(e);
                        auto  matrix    = makeTransformMatrix(transform);

                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetDrawlist();
                        ImGuizmo::SetRect(imagePos.x, imagePos.y, avail.x, avail.y);
                        if (ImGuizmo::Manipulate(glm::value_ptr(editorCamera.view),
                                                 glm::value_ptr(editorCamera.projection),
                                                 toGizmoOperation(m_Tool),
                                                 ImGuizmo::LOCAL,
                                                 glm::value_ptr(matrix)))
                        {
                            float translation[3] {};
                            float rotation[3] {};
                            float scale[3] {};
                            ImGuizmo::DecomposeMatrixToComponents(
                                glm::value_ptr(matrix), translation, rotation, scale);
                            transform.position = {translation[0], translation[1], translation[2]};
                            transform.rotation = glm::quat(glm::radians(glm::vec3 {rotation[0], rotation[1], rotation[2]}));
                            transform.scale    = {scale[0], scale[1], scale[2]};
                            transform.dirty    = true;
                        }
                    }
                }
            }
        }

        ImGui::End();
    }

    void SceneViewWindow::drawToolbar()
    {
        auto toolButton = [&](const char* icon, const char* label, Tool tool)
        {
            if (ui::toolbarToggle(icon, label, m_Tool == tool))
                m_Tool = tool;
        };

        toolButton(ICON_MDI_CURSOR_DEFAULT, "Select", Tool::Select);
        ImGui::SameLine();
        toolButton(ICON_MDI_AXIS_ARROW, "Move", Tool::Move);
        ImGui::SameLine();
        toolButton(ICON_MDI_ROTATE_3D, "Rotate", Tool::Rotate);
        ImGui::SameLine();
        toolButton(ICON_MDI_RESIZE, "Scale", Tool::Scale);
        ImGui::SameLine();
        if (ui::toolbarToggle(ICON_MDI_GRID, "Grid", m_ShowGrid))
            m_ShowGrid = !m_ShowGrid;
    }

    void SceneViewWindow::ensureRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services)
            return;
        if (width == 0u || height == 0u)
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
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                .build(rd);
        m_PendingRenderTarget.textureId     = imguiService->addTexture(*m_PendingRenderTarget.texture);
        m_PendingRenderTarget.frameCreated  = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_PendingRenderTarget.releaseFrame  = 0;
    }

    void SceneViewWindow::promotePendingRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_PendingRenderTarget.texture)
            return;

        retireRenderTarget(m_ActiveRenderTarget);
        m_ActiveRenderTarget  = std::move(m_PendingRenderTarget);
        m_PendingRenderTarget = {};
    }

    void SceneViewWindow::retireRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture && !slot.textureId)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_RetiredRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void SceneViewWindow::collectRetiredRenderTargets(EditorContext& ctx)
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

    void SceneViewWindow::releaseRenderTarget(EditorContext& ctx)
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
} // namespace vultra_app
