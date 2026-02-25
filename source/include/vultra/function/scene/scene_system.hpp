#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/scene/scene_reflection.hpp"
#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/services/scene_service.hpp"

#include <filesystem>
#include <optional>

namespace vultra
{
    class IAssetService;

    class SceneSystem final : public EngineSubsystem, public ISceneService
    {
    public:
        ENGINE_SUBSYSTEM(SceneSystem)

        bool onInit() override;
        void onShutdown() override;

        // ISceneService
        bool hasSceneLoaded() const override;
        bool loadSceneSync(std::string_view uri) override;
        bool saveSceneSync(std::string_view uri) const override;
        bool instantiateToWorld(World& world, bool clearWorld) const override;

        // Accessors for tooling/editor (asset-side). Keep separate from World.
        const VSceneDocument* loadedScene() const;

    private:
        bool instantiateNodeProps(entt::registry& reg, entt::entity ent, const VSceneDocument::Node& node) const;

    private:
        SceneComponentRegistry        m_ComponentRegistry;
        std::optional<VSceneDocument> m_Doc;
        std::filesystem::path         m_LoadedPath;

        IAssetService* m_AssetService {nullptr};
    };
} // namespace vultra
