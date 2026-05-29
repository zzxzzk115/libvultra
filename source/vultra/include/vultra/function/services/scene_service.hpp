#pragma once

#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/world/world.hpp"

#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <string>

namespace vultra
{
    struct SceneDocument;

    enum class SceneLoadState : uint8_t
    {
        eInvalid = 0,
        eLoading,
        eReady,
        eFailed,
    };

    struct SceneLoadHandle
    {
        uint64_t id {0};
        explicit operator bool() const { return id != 0; }
    };

    struct SceneLoadStatus
    {
        SceneLoadState state {SceneLoadState::eInvalid};
        float          progress {0.0f};
        std::string    message;
    };

    class ISceneService
    {
    public:
        SERVICE_REGISTER(ISceneService)
        virtual ~ISceneService() = default;

        // Load a .vscn into a document (asset side). Cached by uri.
        virtual std::shared_ptr<const SceneDocument> loadSceneSync(std::string_view uri) = 0;

        // Prepare a scene without exposing partially-loaded assets to the target world.
        // Poll sceneLoadStatus() until eReady, then call instantiateLoadedScene().
        virtual SceneLoadHandle loadSceneAsync(std::string_view uri) = 0;
        virtual SceneLoadStatus sceneLoadStatus(SceneLoadHandle handle) = 0;
        virtual entt::entity instantiateLoadedScene(SceneLoadHandle handle,
                                                    World&          world,
                                                    entt::entity    parent,
                                                    bool            clearWorld) = 0;
        virtual void releaseSceneLoad(SceneLoadHandle handle) = 0;

        // Streaming path: instantiate immediately and let assets become ready over time.
        // This is useful for editor previews/debugging and future large-world streaming.
        virtual entt::entity
        loadSceneStreaming(World& world, std::string_view uri, entt::entity parent, bool clearWorld) = 0;

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

        virtual SceneDocument captureWorldAsScene(World& world, entt::entity root) = 0;
        virtual entt::entity
        instantiateSceneDocument(World& world, const SceneDocument& doc, entt::entity parent, bool clearWorld) = 0;
    };
} // namespace vultra
