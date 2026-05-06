#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <entt/entity/entity.hpp>

#include <array>

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
        void drawEntityNode(EditorContext& ctx, vultra::World& world, entt::entity entity);

        entt::entity          m_RenameEntity {entt::null};
        std::array<char, 128> m_RenameBuffer {};
    };
} // namespace vultra_app
