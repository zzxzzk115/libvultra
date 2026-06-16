#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/ui_service.hpp"

#include <unordered_map>
#include <vector>

namespace vultra
{
    class UiSystem final : public EngineSubsystem, public IUiService
    {
    public:
        ENGINE_SUBSYSTEM(UiSystem)

        bool onInit() override;
        void onUpdate(fsec dt) override;
        void onPostUpdate(fsec dt) override;
        void onShutdown() override;

        bool pointerOverUi() const override { return m_HoveredEntity != entt::null; }
        entt::entity hoveredEntity() const override { return m_HoveredEntity; }
        entt::entity pressedEntity() const override { return m_PressedEntity; }
        bool buttonClicked(entt::entity entity) const override;
        std::optional<UiResolvedRect> resolvedRect(entt::entity entity) const override;
        std::optional<UiRaycastHit> raycast(glm::vec2 screenPx) const override;
        std::optional<UiRaycastHit> raycastCanvas(entt::entity canvasEntity, glm::vec2 canvasPx) const override;
        const std::vector<UiPointerEvent>& eventsThisFrame() const override { return m_Events; }
        void setInputViewport(bool active, glm::vec2 mousePx, glm::vec2 renderSizePx) override;

        const std::vector<UiResolvedRect>& resolvedRects() const { return m_Rects; }

    private:
        void rebuild();
        void updateInput();
        void pushEvent(UiEventType type,
                       entt::entity target,
                       glm::vec2 screenPx,
                       uint32_t button = 0u,
                       uint32_t clickCount = 0u);

        std::vector<UiResolvedRect> m_Rects;
        std::vector<UiPointerEvent> m_Events;
        std::unordered_map<entt::entity, std::size_t> m_RectByEntity;
        entt::entity m_HoveredEntity {entt::null};
        entt::entity m_PreviousHoveredEntity {entt::null};
        entt::entity m_PressedEntity {entt::null};
        entt::entity m_FocusedEntity {entt::null}; // text input field / expanded dropdown
        uint64_t     m_NextEventSequence {1};

        // Embedded-viewport input override (editor Game View). See setInputViewport().
        struct InputViewport
        {
            bool      active {false};
            glm::vec2 mousePx {0.0f};
            glm::vec2 renderSizePx {0.0f};
        };
        InputViewport m_InputViewport;
    };
} // namespace vultra
