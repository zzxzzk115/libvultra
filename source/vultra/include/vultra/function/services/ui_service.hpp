#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <vbase/service/service_registry.hpp>

#include <optional>

namespace vultra
{
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

    class IUiService
    {
    public:
        SERVICE_REGISTER(IUiService)

        virtual bool pointerOverUi() const = 0;
        virtual entt::entity hoveredEntity() const = 0;
        virtual entt::entity pressedEntity() const = 0;
        virtual bool buttonClicked(entt::entity entity) const = 0;
        virtual std::optional<UiResolvedRect> resolvedRect(entt::entity entity) const = 0;
    };
} // namespace vultra
