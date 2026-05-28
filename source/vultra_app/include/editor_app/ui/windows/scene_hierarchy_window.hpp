#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/base/uuid.hpp>

#include <entt/entity/entity.hpp>

#include <array>
#include <string>

namespace vultra
{
    class World;
}

namespace vultra_app
{
    class SceneHierarchyWindow final : public EditorWindow
    {
    public:
        SceneHierarchyWindow();

        void draw(EditorContext& ctx) override;

    private:
        struct PendingAssetInstantiation
        {
            vultra::CoreUUID uuid;
            entt::entity     parent {entt::null};
            entt::entity     beforeSibling {entt::null};
            entt::entity     afterSibling {entt::null};
            bool             keepPosition {false};
            bool             keepRotation {false};
            bool             keepScale {true};
            bool             openPopup {false};
        };

        void drawEntityNode(EditorContext& ctx, vultra::World& world, entt::entity entity, const char* filter);
        bool entityMatchesFilter(vultra::World& world, entt::entity entity, const char* filter) const;
        bool acceptAssetDrop(EditorContext& ctx,
                             vultra::World& world,
                             entt::entity parent,
                             entt::entity beforeSibling = entt::null,
                             entt::entity afterSibling = entt::null);
        void drawPendingAssetInstantiationPopup(EditorContext& ctx, vultra::World& world);
        bool completeAssetInstantiation(EditorContext& ctx, vultra::World& world, const PendingAssetInstantiation& request);

        entt::entity          m_RenameEntity {entt::null};
        std::array<char, 128> m_RenameBuffer {};
        std::array<char, 128> m_SearchBuffer {};
        PendingAssetInstantiation m_PendingAssetInstantiation {};
    };
} // namespace vultra_app
