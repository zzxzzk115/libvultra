#include "vultra/function/ui/ui_system.hpp"

#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/script_service.hpp"
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

        [[nodiscard]] glm::vec2 canvasOffset(const CanvasComponent& canvas, glm::vec2 windowPx, glm::vec2 scale)
        {
            const auto canvasSize = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f}) * scale;
            return (windowPx - canvasSize) * 0.5f;
        }

        [[nodiscard]] bool uiInteractable(const entt::registry& reg, const entt::entity entity)
        {
            if (const auto* button = reg.try_get<UiButtonComponent>(entity))
                return button->enabled && button->interactable;
            if (const auto* toggle = reg.try_get<UiToggleComponent>(entity))
                return toggle->enabled && toggle->interactable;
            if (const auto* slider = reg.try_get<UiSliderComponent>(entity))
                return slider->enabled && slider->interactable;
            return false;
        }

        void setSliderFromPointer(UiSliderComponent& slider, const UiResolvedRect& rect, const glm::vec2 screenPx)
        {
            const float width = std::max(rect.maxPx.x - rect.minPx.x, 1.0f);
            const float t = std::clamp((screenPx.x - rect.minPx.x) / width, 0.0f, 1.0f);
            slider.value = slider.minValue + (slider.maxValue - slider.minValue) * t;
        }

        [[nodiscard]] bool runtimeUiInputEnabled(vbase::ServiceRegistry& services)
        {
            if (const auto* scripts = services.tryGet<IScriptService>())
                return scripts->isPlaybackPlaying() && !scripts->isPlaybackPaused();
            return true;
        }

        void resolveRectTopLeft(const RectTransformComponent& rect,
                                const glm::vec2&             parentMin,
                                const glm::vec2&             parentSize,
                                const glm::vec2&             canvasScale,
                                glm::vec2&                   outMinPx,
                                glm::vec2&                   outSizePx)
        {
            const glm::vec2 anchorMin = parentMin + parentSize * rect.anchorMin;
            const glm::vec2 anchorMax = parentMin + parentSize * rect.anchorMax;
            outSizePx                 = (anchorMax - anchorMin) + rect.sizeDeltaPx * canvasScale;
            outMinPx                  = anchorMin + rect.anchoredPositionPx * canvasScale - outSizePx * rect.pivot;
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
        m_Events.clear();
        m_RectByEntity.clear();
        m_HoveredEntity = entt::null;
        m_PreviousHoveredEntity = entt::null;
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

    std::optional<UiRaycastHit> UiSystem::raycast(glm::vec2 screenPx) const
    {
        for (auto it = m_Rects.rbegin(); it != m_Rects.rend(); ++it)
        {
            if (!contains(*it, screenPx))
                continue;

            const auto canvasRect = resolvedRect(it->canvas);
            UiRaycastHit hit {};
            hit.entity    = it->entity;
            hit.canvas    = it->canvas;
            hit.screenPx  = screenPx;
            hit.canvasPx  = canvasRect ? screenPx - canvasRect->minPx : screenPx;
            hit.localPx   = screenPx - it->minPx;
            hit.sortOrder = it->sortOrder;
            hit.depth     = it->depth;
            return hit;
        }
        return std::nullopt;
    }

    std::optional<UiRaycastHit> UiSystem::raycastCanvas(entt::entity canvasEntity, glm::vec2 canvasPx) const
    {
        const auto canvasRect = resolvedRect(canvasEntity);
        if (!canvasRect)
            return std::nullopt;
        auto hit = raycast(canvasRect->minPx + canvasPx);
        if (!hit || hit->canvas != canvasEntity)
            return std::nullopt;
        hit->canvasPx = canvasPx;
        return hit;
    }

    void UiSystem::pushEvent(UiEventType type,
                             entt::entity target,
                             glm::vec2 screenPx,
                             uint32_t button,
                             uint32_t clickCount)
    {
        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService || target == entt::null)
            return;

        auto& world = worldService->world();
        const auto targetRect = resolvedRect(target);
        const entt::entity canvas = targetRect ? targetRect->canvas : entt::null;
        const auto canvasRect = canvas != entt::null ? resolvedRect(canvas) : std::optional<UiResolvedRect> {};
        const glm::vec2 canvasPx = canvasRect ? screenPx - canvasRect->minPx : screenPx;
        const uint64_t sequence = m_NextEventSequence++;

        for (auto current = target; current != entt::null && world.registry().valid(current); current = world.parent(current))
        {
            const auto currentRect = resolvedRect(current);
            UiPointerEvent event {};
            event.type            = type;
            event.target          = target;
            event.currentTarget   = current;
            event.canvas          = canvas;
            event.screenPosition  = screenPx;
            event.canvasPosition  = canvasPx;
            event.localPosition   = currentRect ? screenPx - currentRect->minPx : glm::vec2 {0.0f};
            event.button          = button;
            event.clickCount      = clickCount;
            event.sequence        = sequence;
            m_Events.push_back(event);
            if (current == canvas)
                break;
        }
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

            glm::vec2 minPx {};
            glm::vec2 sizePx {};
            resolveRectTopLeft(*rect, parentMin, parentSize, scale, minPx, sizePx);

            UiResolvedRect resolved {};
            resolved.entity       = entity;
            resolved.canvas       = canvasEntity;
            resolved.minPx        = minPx;
            resolved.maxPx        = minPx + sizePx * rect->scale;
            resolved.sortOrder    = sortOrder;
            resolved.depth        = depth;
            resolved.interactable = uiInteractable(reg, entity);
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
            const glm::vec2 offset = canvasOffset(canvas, windowPx, scale);
            const glm::vec2 canvasSize = glm::max(canvas.referenceResolutionPx * scale, glm::vec2 {1.0f});
            UiResolvedRect root {};
            root.entity    = canvasEntity;
            root.canvas    = canvasEntity;
            root.minPx     = offset;
            root.maxPx     = offset + canvasSize;
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
        m_Events.clear();
        for (auto entity : reg.view<UiButtonComponent>())
        {
            auto& button = reg.get<UiButtonComponent>(entity);
            button.hovered = false;
            button.pressed = false;
        }

        if (!runtimeUiInputEnabled(ctx().services))
        {
            m_PreviousHoveredEntity = entt::null;
            m_HoveredEntity = entt::null;
            m_PressedEntity = entt::null;
            if (auto* cameraService = ctx().services.tryGet<ICameraService>())
                cameraService->setCameraControlInputSuppressed(false);
            return;
        }

        const glm::vec2 mouse = input->mousePosition();
        m_PreviousHoveredEntity = m_HoveredEntity;
        m_HoveredEntity = entt::null;
        if (auto hit = raycast(mouse))
            m_HoveredEntity = hit->entity;

        if (m_PreviousHoveredEntity != m_HoveredEntity)
        {
            if (m_PreviousHoveredEntity != entt::null)
                pushEvent(UiEventType::PointerExit, m_PreviousHoveredEntity, mouse);
            if (m_HoveredEntity != entt::null)
                pushEvent(UiEventType::PointerEnter, m_HoveredEntity, mouse);
        }
        if (m_HoveredEntity != entt::null)
            pushEvent(UiEventType::PointerMove, m_HoveredEntity, mouse);

        if (auto* button = reg.try_get<UiButtonComponent>(m_HoveredEntity); button && button->enabled)
            button->hovered = true;

        if (m_HoveredEntity != entt::null && input->isMouseButtonPressed(MouseCode::eLeft))
        {
            m_PressedEntity = m_HoveredEntity;
            pushEvent(UiEventType::PointerDown, m_PressedEntity, mouse, 0u);
            if (auto* slider = reg.try_get<UiSliderComponent>(m_PressedEntity); slider && slider->enabled && slider->interactable)
                if (auto rect = resolvedRect(m_PressedEntity))
                    setSliderFromPointer(*slider, *rect, mouse);
        }

        if (m_PressedEntity != entt::null)
        {
            if (auto* button = reg.try_get<UiButtonComponent>(m_PressedEntity))
                button->pressed = input->isMouseButtonHeld(MouseCode::eLeft);
            if (auto* slider = reg.try_get<UiSliderComponent>(m_PressedEntity); slider && slider->enabled && slider->interactable)
                if (input->isMouseButtonHeld(MouseCode::eLeft))
                    if (auto rect = resolvedRect(m_PressedEntity))
                        setSliderFromPointer(*slider, *rect, mouse);
            if (input->isMouseButtonReleased(MouseCode::eLeft))
            {
                pushEvent(UiEventType::PointerUp, m_PressedEntity, mouse, 0u);
                if (m_HoveredEntity == m_PressedEntity)
                {
                    pushEvent(UiEventType::Click, m_PressedEntity, mouse, 0u, 1u);
                    if (auto* button = reg.try_get<UiButtonComponent>(m_PressedEntity))
                        if (button->enabled && button->interactable)
                            button->clicked = true;
                    if (auto* toggle = reg.try_get<UiToggleComponent>(m_PressedEntity))
                        if (toggle->enabled && toggle->interactable)
                            toggle->checked = !toggle->checked;
                }
                m_PressedEntity = entt::null;
            }
        }

        if (auto* cameraService = ctx().services.tryGet<ICameraService>())
            cameraService->setCameraControlInputSuppressed(m_HoveredEntity != entt::null || m_PressedEntity != entt::null);
    }
} // namespace vultra
