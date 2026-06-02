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

        const std::vector<UiResolvedRect>& resolvedRects() const { return m_Rects; }

    private:
        void rebuild();
        void updateInput();

        std::vector<UiResolvedRect> m_Rects;
        std::unordered_map<entt::entity, std::size_t> m_RectByEntity;
        entt::entity m_HoveredEntity {entt::null};
        entt::entity m_PressedEntity {entt::null};
    };
} // namespace vultra
