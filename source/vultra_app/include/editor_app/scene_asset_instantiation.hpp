#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/base/uuid.hpp>

#include <entt/entity/entity.hpp>
#include <nlohmann/json_fwd.hpp>

namespace vultra
{
    class World;
}

namespace vultra_app
{
    struct AssetInstantiationOptions
    {
        entt::entity parent {entt::null};
        entt::entity beforeSibling {entt::null};
        entt::entity afterSibling {entt::null};
        bool         keepPosition {true};
        bool         keepRotation {true};
        bool         keepScale {true};
    };

    bool shouldPromptMeshSubAssetPlacement(EditorContext& ctx, const vultra::CoreUUID& uuid);

    entt::entity instantiateAssetInScene(EditorContext&                     ctx,
                                         vultra::World&                     world,
                                         const vultra::CoreUUID&            uuid,
                                         const AssetInstantiationOptions& options,
                                         nlohmann::json*                   out = nullptr);
} // namespace vultra_app
