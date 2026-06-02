#include "vultra/function/ui/ui_system.hpp"

#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace
    {
        [[nodiscard]] bool visible(const entt::registry& reg, entt::entity entity)
        {
            if (const auto* status = reg.try_get<EntityStatusComponent>(entity))
                return status->active && status->visible;
            return true;
        }

        [[nodiscard]] bool contains(const UiResolvedRect& rect, glm::vec2 p)
        {
            return p.x >= rect.minPx.x && p.x <= rect.maxPx.x && p.y >= rect.minPx.y && p.y <= rect.maxPx.y;
        }

        [[nodiscard]] glm::vec2 windowExtentPx(vbase::ServiceRegistry& services)
        {
            if (auto* windowService = services.tryGet<IWindowService>())
            {
                const auto extent = windowService->window().getExtent();
                return {static_cast<float>(std::max(extent.x, 1)), static_cast<float>(std::max(extent.y, 1))};
            }
            return {1920.0f, 1080.0f};
        }

        [[nodiscard]] glm::vec2 canvasScale(const CanvasComponent& canvas, glm::vec2 windowPx)
        {
            const auto reference = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
            if (canvas.scaleMode == 1u)
            {
                const float scale = std::min(windowPx.x / reference.x, windowPx.y / reference.y);
                return {scale, scale};
            }
            return {1.0f, 1.0f};
        }
    } // namespace

    bool UiSystem::onInit()
    {
        ctx().services.provide<IUiService>(this);
        return true;
    }

    void UiSystem::onShutdown()
    {
        m_Rects.clear();
        m_RectByEntity.clear();
        m_HoveredEntity = entt::null;
        m_PressedEntity = entt::null;
    }

    void UiSystem::onUpdate(fsec)
    {
        rebuild();
        updateInput();
    }

    void UiSystem::onPostUpdate(fsec)
    {
        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService)
            return;
        auto& reg = worldService->world().registry();
        for (auto entity : reg.view<UiButtonComponent>())
            reg.get<UiButtonComponent>(entity).clicked = false;
    }

    bool UiSystem::buttonClicked(entt::entity entity) const
    {
        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService)
            return false;
        if (auto* button = worldService->world().registry().try_get<UiButtonComponent>(entity))
            return button->clicked;
        return false;
    }

    std::optional<UiResolvedRect> UiSystem::resolvedRect(entt::entity entity) const
    {
        if (const auto it = m_RectByEntity.find(entity); it != m_RectByEntity.end() && it->second < m_Rects.size())
            return m_Rects[it->second];
        return std::nullopt;
    }

    void UiSystem::rebuild()
    {
        m_Rects.clear();
        m_RectByEntity.clear();

        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService)
            return;

        auto& world = worldService->world();
        auto& reg   = world.registry();
        const auto windowPx = windowExtentPx(ctx().services);

        auto pushChildren = [&](auto&& self,
                                entt::entity canvasEntity,
                                entt::entity entity,
                                const glm::vec2 parentMin,
                                const glm::vec2 parentSize,
                                const glm::vec2 scale,
                                int sortOrder,
                                uint32_t depth) -> void {
            const auto* rect = reg.try_get<RectTransformComponent>(entity);
            if (!rect || !visible(reg, entity))
                return;

            glm::vec2 minPx = parentMin;
            glm::vec2 sizePx = parentSize;
            const glm::vec2 anchorMin = parentMin + parentSize * rect->anchorMin;
            const glm::vec2 anchorMax = parentMin + parentSize * rect->anchorMax;
            sizePx = (anchorMax - anchorMin) + rect->sizeDeltaPx * scale;
            minPx  = anchorMin + rect->anchoredPositionPx * scale - sizePx * rect->pivot;

            UiResolvedRect resolved {};
            resolved.entity       = entity;
            resolved.canvas       = canvasEntity;
            resolved.minPx        = minPx;
            resolved.maxPx        = minPx + sizePx * rect->scale;
            resolved.sortOrder    = sortOrder;
            resolved.depth        = depth;
            resolved.interactable = reg.all_of<UiButtonComponent>(entity);
            m_RectByEntity[entity] = m_Rects.size();
            m_Rects.push_back(resolved);

            const auto* layout = reg.try_get<UiLayoutComponent>(entity);
            uint32_t childIndex = 0;
            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
            {
                if (layout && layout->enabled && layout->kind != 0u)
                {
                    auto* childRect = reg.try_get<RectTransformComponent>(child);
                    if (childRect)
                    {
                        const glm::vec2 innerMin =
                            resolved.minPx + glm::vec2 {layout->paddingPx.x, layout->paddingPx.y} * scale;
                        const glm::vec2 innerMax =
                            resolved.maxPx - glm::vec2 {layout->paddingPx.z, layout->paddingPx.w} * scale;
                        const glm::vec2 cell = layout->cellSizePx * scale;
                        if (layout->kind == 1u)
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x + childIndex * (layout->cellSizePx.x + layout->spacingPx),
                                layout->paddingPx.y};
                        else if (layout->kind == 2u)
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x,
                                layout->paddingPx.y + childIndex * (layout->cellSizePx.y + layout->spacingPx)};
                        else if (layout->kind == 3u)
                        {
                            const uint32_t columns =
                                std::max(1u, static_cast<uint32_t>(std::floor((innerMax.x - innerMin.x) /
                                                                               std::max(cell.x + layout->spacingPx, 1.0f))));
                            childRect->anchoredPositionPx = glm::vec2 {
                                layout->paddingPx.x + (childIndex % columns) * (layout->cellSizePx.x + layout->spacingPx),
                                layout->paddingPx.y + (childIndex / columns) * (layout->cellSizePx.y + layout->spacingPx)};
                        }
                        childRect->anchorMin = {0.0f, 0.0f};
                        childRect->anchorMax = {0.0f, 0.0f};
                        childRect->pivot     = {0.0f, 0.0f};
                        childRect->sizeDeltaPx = layout->cellSizePx;
                    }
                }
                self(self, canvasEntity, child, resolved.minPx, resolved.maxPx - resolved.minPx, scale, sortOrder, depth + 1u);
                ++childIndex;
            }
        };

        auto canvasView = reg.view<CanvasComponent>();
        for (auto canvasEntity : canvasView)
        {
            const auto& canvas = canvasView.get<CanvasComponent>(canvasEntity);
            if (!canvas.enabled || !visible(reg, canvasEntity))
                continue;
            const glm::vec2 scale = canvasScale(canvas, windowPx);
            const glm::vec2 canvasSize = glm::max(canvas.referenceResolutionPx * scale, glm::vec2 {1.0f});
            UiResolvedRect root {};
            root.entity    = canvasEntity;
            root.canvas    = canvasEntity;
            root.minPx     = {0.0f, 0.0f};
            root.maxPx     = canvasSize;
            root.sortOrder = canvas.sortOrder;
            root.depth     = 0;
            m_RectByEntity[canvasEntity] = m_Rects.size();
            m_Rects.push_back(root);

            for (auto child = world.firstChild(canvasEntity); child != entt::null; child = world.nextSibling(child))
                pushChildren(pushChildren, canvasEntity, child, root.minPx, canvasSize, scale, canvas.sortOrder, 1u);
        }

        std::sort(m_Rects.begin(), m_Rects.end(), [](const UiResolvedRect& a, const UiResolvedRect& b) {
            if (a.sortOrder != b.sortOrder)
                return a.sortOrder < b.sortOrder;
            return a.depth < b.depth;
        });
        m_RectByEntity.clear();
        for (std::size_t i = 0; i < m_Rects.size(); ++i)
            m_RectByEntity[m_Rects[i].entity] = i;
    }

    void UiSystem::updateInput()
    {
        auto* worldService = ctx().services.tryGet<IWorldService>();
        auto* input = ctx().services.tryGet<IInputService>();
        if (!worldService || !input)
            return;

        auto& reg = worldService->world().registry();
        for (auto entity : reg.view<UiButtonComponent>())
        {
            auto& button = reg.get<UiButtonComponent>(entity);
            button.hovered = false;
            button.pressed = false;
        }

        const glm::vec2 mouse = input->getMousePosition();
        m_HoveredEntity = entt::null;
        for (auto it = m_Rects.rbegin(); it != m_Rects.rend(); ++it)
        {
            if (!it->interactable || !contains(*it, mouse))
                continue;
            if (auto* button = reg.try_get<UiButtonComponent>(it->entity); button && button->enabled && button->interactable)
            {
                m_HoveredEntity = it->entity;
                button->hovered = true;
                break;
            }
        }

        if (m_HoveredEntity != entt::null && input->getMouseButtonDown(MouseCode::eLeft))
            m_PressedEntity = m_HoveredEntity;

        if (m_PressedEntity != entt::null)
        {
            if (auto* button = reg.try_get<UiButtonComponent>(m_PressedEntity))
                button->pressed = input->getMouseButton(MouseCode::eLeft);
            if (input->getMouseButtonUp(MouseCode::eLeft))
            {
                if (m_HoveredEntity == m_PressedEntity)
                    if (auto* button = reg.try_get<UiButtonComponent>(m_PressedEntity))
                        button->clicked = true;
                m_PressedEntity = entt::null;
            }
        }

        if (auto* cameraService = ctx().services.tryGet<ICameraService>())
            cameraService->setCameraControlInputSuppressed(m_HoveredEntity != entt::null || m_PressedEntity != entt::null);
    }
} // namespace vultra
