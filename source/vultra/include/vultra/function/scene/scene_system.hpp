#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/scene/scene_component_registry.hpp"
#include "vultra/function/services/scene_service.hpp"

#include <vbase/core/result.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace vultra
{
    struct SceneDocument;
    struct SceneNode;

    class IAssetService;

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
        bool                                 saveSceneSync(std::string_view uri, const SceneDocument& doc) override;

        entt::entity
             instantiateScene(World& world, std::string_view uri, entt::entity parent, bool clearWorld) override;
        bool saveWorldAsSceneSync(std::string_view uri, World& world, entt::entity root) override;
        SceneDocument captureWorldAsScene(World& world, entt::entity root) override;
        entt::entity
        instantiateSceneDocument(World& world, const SceneDocument& doc, entt::entity parent, bool clearWorld) override;

    private:
        SceneComponentRegistry m_ComponentRegistry;

        std::unordered_map<std::string, std::shared_ptr<const SceneDocument>> m_Cache;

        IAssetService* m_AssetService {nullptr};

        std::filesystem::path toPath(std::string_view uri);

        entt::entity
        instantiateNode(World& world, const SceneNode& node, entt::entity parent, const std::filesystem::path& baseDir);

        void applyProperties(entt::registry& reg,
                     entt::entity e,
                     const SceneNode& node,
                     const std::unordered_map<std::string, std::string>& assets);

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

        // World -> Scene
        BuildNodeResult buildNodeFromWorldR(World& world, entt::entity e);
    };
} // namespace vultra
