#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/scene/scene_component_registry.hpp"
#include "vultra/function/services/scene_service.hpp"

#include <vbase/core/result.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vultra
{
    struct SceneDocument;
    struct SceneNode;

    class IAssetService;
    class IRenderService;
    class IWorldService;

    class SceneSystem final : public EngineSubsystem, public ISceneService
    {
    public:
        ENGINE_SUBSYSTEM(SceneSystem)

        SceneSystem()           = default;
        ~SceneSystem() override = default;

        bool onInit() override;
        void onShutdown() override;

        // ISceneService
        std::shared_ptr<const SceneDocument> loadSceneSync(std::string_view uri) override;
        SceneLoadHandle                      loadSceneAsync(std::string_view uri) override;
        SceneLoadStatus                      sceneLoadStatus(SceneLoadHandle handle) override;
        entt::entity                         instantiateLoadedScene(SceneLoadHandle handle,
                                                                    World&          world,
                                                                    entt::entity    parent,
                                                                    bool            clearWorld) override;
        void                                 releaseSceneLoad(SceneLoadHandle handle) override;
        entt::entity
        loadSceneStreaming(World& world, std::string_view uri, entt::entity parent, bool clearWorld) override;
        bool                                 saveSceneSync(std::string_view uri, const SceneDocument& doc) override;

        entt::entity
             instantiateScene(World& world, std::string_view uri, entt::entity parent, bool clearWorld) override;
        entt::entity instantiatePrefab(World& world, std::string_view prefabUri, entt::entity parent) override;
        bool saveWorldAsSceneSync(std::string_view uri, World& world, entt::entity root) override;
        SceneDocument captureWorldAsScene(World& world, entt::entity root) override;
        entt::entity
        instantiateSceneDocument(World& world, const SceneDocument& doc, entt::entity parent, bool clearWorld) override;

        std::unordered_set<std::string> prefabOverriddenFields(World& world, entt::entity e) override;
        bool revertPrefabField(World& world, entt::entity e, std::string_view component, std::string_view field) override;
        bool applyPrefabField(World& world, entt::entity e, std::string_view component, std::string_view field) override;

    private:
        SceneComponentRegistry m_ComponentRegistry;

        std::unordered_map<std::string, std::shared_ptr<const SceneDocument>> m_Cache;

        struct AsyncSceneLoad
        {
            std::string                          uri;
            std::shared_ptr<const SceneDocument> doc;
            std::unique_ptr<World>               stagingWorld;
            entt::entity                         stagingRoot {entt::null};
            SceneLoadState                       state {SceneLoadState::eLoading};
            float                                progress {0.0f};
            std::string                          message;
        };
        uint64_t m_NextAsyncSceneLoadId {1};
        std::unordered_map<uint64_t, AsyncSceneLoad> m_AsyncSceneLoads;

        IAssetService* m_AssetService {nullptr};

        std::filesystem::path toPath(std::string_view uri);

        void clearWorldForSceneReplacement(World& world);

        entt::entity
        instantiateNode(World& world, const SceneNode& node, entt::entity parent, const std::filesystem::path& baseDir);

        void applyProperties(entt::registry& reg,
                     entt::entity e,
                     const SceneNode& node,
                     const std::unordered_map<std::string, std::string>& assets);

        void applyMeshDefaultTransformIfNeeded(entt::registry& reg, entt::entity e, const SceneNode& node);

        entt::meta_any parseValueToAny(entt::meta_type expected,
                           std::string_view raw,
                           const std::unordered_map<std::string, std::string>& assets) const;

        static std::string trim(std::string_view s);

        using InstantiateNodeResult = vbase::Result<entt::entity, std::string>;
        using BuildNodeResult       = vbase::Result<std::unique_ptr<SceneNode>, std::string>;

        InstantiateNodeResult
        instantiateNodeR(World& world,
                 const SceneNode& node,
                 entt::entity parent,
                 const std::filesystem::path& baseDir,
                 bool allowPrefab,
                 const std::unordered_map<std::string, std::string>& assets);

        // Prefab support.
        // Instantiates the prefab's own subtree, assigning each descendant a deterministic
        // per-instance UUID derived from (instanceRoot, prefab-internal node id). The prefab
        // root entity itself is assigned `instanceRoot` (the scene's instance node uuid).
        InstantiateNodeResult
        instantiatePrefabContentR(World&                                              world,
                                  const SceneNode&                                    prefabNode,
                                  entt::entity                                        parent,
                                  const std::filesystem::path&                        baseDir,
                                  const std::unordered_map<std::string, std::string>& assets,
                                  const CoreUUID&                                     instanceRoot,
                                  bool                                                isRoot);

        // Applies a scene instance node's child entry as either an override on an existing
        // prefab descendant (matched by uuid in `index`) or a newly-added child subtree.
        void applyInstanceOverridesR(World&                                              world,
                                     const SceneNode&                                    childNode,
                                     entt::entity                                        parentEntity,
                                     const std::unordered_map<CoreUUID, entt::entity>&   index,
                                     const std::filesystem::path&                        baseDir,
                                     const std::unordered_map<std::string, std::string>& assets);

        // Resolves the prefab source node corresponding to `e` inside the prefab instance
        // containing it; `doc` keeps `srcNode` alive. Shared by the prefab field-diff APIs.
        struct PrefabSourceLookup
        {
            std::shared_ptr<const SceneDocument> doc;
            const SceneNode*                     srcNode {nullptr};
            CoreUUID                             entityUuid;
            std::string                          prefabUri;
        };
        bool resolvePrefabSource(World& world, entt::entity e, PrefabSourceLookup& out);

        // World -> Scene
        BuildNodeResult buildNodeFromWorldR(World& world, entt::entity e);

        // World -> Scene for a prefab instance: emits only fields that differ from the prefab
        // source and only user-added (non-prefab) children, plus override nodes for changed
        // prefab descendants.
        BuildNodeResult buildInstanceNodeFromWorldR(World&                                                world,
                                                    entt::entity                                          e,
                                                    const SceneNode*                                      prefabNode,
                                                    const std::unordered_map<CoreUUID, const SceneNode*>& srcIndex,
                                                    bool                                                  isRoot);
    };
} // namespace vultra
