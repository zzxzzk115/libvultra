#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra
{
    enum class UiEventType : uint32_t
    {
        PointerEnter,
        PointerExit,
        PointerMove,
        PointerDown,
        PointerUp,
        Click,
    };

    struct UiResolvedRect
    {
        entt::entity entity {entt::null};
        entt::entity canvas {entt::null};
        glm::vec2    minPx {0.0f};
        glm::vec2    maxPx {0.0f};
        int          sortOrder {0};
        uint32_t     depth {0};
        bool         interactable {false};
    };

    struct UiRaycastHit
    {
        entt::entity entity {entt::null};
        entt::entity canvas {entt::null};
        glm::vec2    screenPx {0.0f};
        glm::vec2    canvasPx {0.0f};
        glm::vec2    localPx {0.0f};
        int          sortOrder {0};
        uint32_t     depth {0};
    };

    struct UiPointerEvent
    {
        UiEventType  type {UiEventType::PointerMove};
        entt::entity target {entt::null};
        entt::entity currentTarget {entt::null};
        entt::entity canvas {entt::null};
        glm::vec2    screenPosition {0.0f};
        glm::vec2    canvasPosition {0.0f};
        glm::vec2    localPosition {0.0f};
        uint32_t     button {0};
        uint32_t     clickCount {0};
        uint64_t     sequence {0};
    };

    class IUiService
    {
    public:
        SERVICE_REGISTER(IUiService)

        virtual bool pointerOverUi() const = 0;
        virtual entt::entity hoveredEntity() const = 0;
        virtual entt::entity pressedEntity() const = 0;
        virtual bool buttonClicked(entt::entity entity) const = 0;
        virtual std::optional<UiResolvedRect> resolvedRect(entt::entity entity) const = 0;
        virtual std::optional<UiRaycastHit> raycast(glm::vec2 screenPx) const = 0;
        virtual std::optional<UiRaycastHit> raycastCanvas(entt::entity canvasEntity, glm::vec2 canvasPx) const = 0;
        virtual const std::vector<UiPointerEvent>& eventsThisFrame() const = 0;

        // Override the UI input source for an embedded viewport (e.g. the editor Game View). When
        // active, UI layout uses renderSizePx as the screen extent and mousePx as the pointer
        // position, both in the render target's pixel space. When inactive (default / standalone
        // runtime), the UI uses the OS window extent and the raw OS mouse. Call every frame.
        virtual void setInputViewport(bool active, glm::vec2 mousePx, glm::vec2 renderSizePx) = 0;
    };
} // namespace vultra
