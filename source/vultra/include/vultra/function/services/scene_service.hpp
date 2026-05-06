#pragma once

#include "vultra/function/world/world.hpp"

#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>

#include <memory>
#include <string_view>

namespace vultra
{
    struct SceneDocument;

    class ISceneService
    {
    public:
        SERVICE_REGISTER(ISceneService)
        virtual ~ISceneService() = default;

        // Load a .vscn into a document (asset side). Cached by uri.
        virtual std::shared_ptr<const SceneDocument> loadSceneSync(std::string_view uri) = 0;

        // Save a document to .vscn (asset side).
        virtual bool saveSceneSync(std::string_view uri, const SceneDocument& doc) = 0;

        // Instantiate a scene into a world (logic side).
        // Returns the root entity of the instantiated scene.
        virtual entt::entity
        instantiateScene(World& world, std::string_view uri, entt::entity parent, bool clearWorld) = 0;

        entt::entity instantiateScene(World& world, std::string_view uri)
        {
            return instantiateScene(world, uri, entt::null, false);
        }

        // Save world (or a subtree) as a .vscn.
        virtual bool saveWorldAsSceneSync(std::string_view uri, World& world, entt::entity root) = 0;

        bool saveWorldAsSceneSync(std::string_view uri, World& world)
        {
            return saveWorldAsSceneSync(uri, world, entt::null);
        }
    };
} // namespace vultra
