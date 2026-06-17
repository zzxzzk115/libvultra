#include "editor_app/scene_asset_instantiation.hpp"

#include "editor_app/editor_history.hpp"
#include "editor_app/selection.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <nlohmann/json.hpp>
#include <vasset/vasset_type.hpp>

#include <filesystem>
#include <optional>

namespace vultra_app
{
    namespace
    {
        std::string assetNameFromEntry(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            const auto sourceName = std::filesystem::path(entry.sourcePath).stem().generic_string();
            if (!sourceName.empty())
                return sourceName;
            const auto importedName = std::filesystem::path(entry.importedPath).stem().generic_string();
            return importedName.empty() ? "Asset" : importedName;
        }

        struct MeshSubAssetPlacement
        {
            std::string                name;
            vultra::TransformComponent transform;
        };

        std::optional<MeshSubAssetPlacement> findMeshSubAssetPlacement(EditorContext&          ctx,
                                                                       const vultra::CoreUUID& meshUuid)
        {
            if (!ctx.services || !meshUuid.valid())
                return std::nullopt;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return std::nullopt;

            auto handle = assetService->loadMeshAsync(meshUuid);
            if (!handle.cpu() || !handle.cpu()->hasDefaultTransform)
                return std::nullopt;

            MeshSubAssetPlacement placement {};
            placement.name               = handle.cpu()->name;
            placement.transform.position = handle.cpu()->defaultPosition;
            placement.transform.rotation = handle.cpu()->defaultRotation;
            placement.transform.scale    = handle.cpu()->defaultScale;
            placement.transform.dirty    = true;
            return placement;
        }

        nlohmann::json entityJson(vultra::World& world, const entt::entity entity)
        {
            auto& reg = world.registry();
            nlohmann::json out {{"entity", static_cast<uint32_t>(entity)}};
            if (const auto* id = reg.try_get<vultra::IDComponent>(entity))
                out["uuid"] = id->uuid.toString();
            if (const auto* name = reg.try_get<vultra::NameComponent>(entity))
                out["name"] = name->name;
            return out;
        }
    } // namespace

    bool shouldPromptMeshSubAssetPlacement(EditorContext& ctx, const vultra::CoreUUID& uuid)
    {
        if (!uuid.valid() || !ctx.services)
            return false;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
            return false;

        const auto entry = assetService->registry().lookup(uuid.native());
        return entry.type == vasset::VAssetType::eMesh && findMeshSubAssetPlacement(ctx, uuid).has_value();
    }

    entt::entity instantiateAssetInScene(EditorContext&                    ctx,
                                         vultra::World&                    world,
                                         const vultra::CoreUUID&           uuid,
                                         const AssetInstantiationOptions& options,
                                         nlohmann::json*                  out)
    {
        if (!uuid.valid() || !ctx.services)
            return entt::null;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
            return entt::null;

        const auto entry = assetService->registry().lookup(uuid.native());
        if (entry.type == vasset::VAssetType::eUnknown)
        {
            ctx.state.statusMessage = "Dropped asset is not registered.";
            return entt::null;
        }

        entt::entity entity = entt::null;
        if (entry.type == vasset::VAssetType::eScene || entry.type == vasset::VAssetType::eSceneManifest ||
            entry.type == vasset::VAssetType::ePrefab)
        {
            auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
            if (!sceneService)
                return entt::null;

            std::string uri;
            if (!assetService->resolver().resolve(uuid.native(), uri))
            {
                ctx.state.statusMessage = "Could not resolve scene asset URI.";
                return entt::null;
            }

            if (entry.type == vasset::VAssetType::ePrefab)
            {
                entity = sceneService->instantiatePrefab(world, uri, options.parent);
                if (entity != entt::null)
                    ctx.state.statusMessage = "Instantiated prefab: " + assetNameFromEntry(entry);
            }
            else
            {
                entity = sceneService->instantiateScene(world, uri, options.parent, false);
                if (entity != entt::null)
                    ctx.state.statusMessage = "Instantiated scene: " + assetNameFromEntry(entry);
            }
        }
        else if (entry.type == vasset::VAssetType::eMesh || entry.type == vasset::VAssetType::eGaussianSplat)
        {
            auto& reg = world.registry();
            entity = options.parent == entt::null ? world.createEntity() : world.createChild(options.parent);
            const auto placement = entry.type == vasset::VAssetType::eMesh ?
                                       findMeshSubAssetPlacement(ctx, uuid) :
                                       std::optional<MeshSubAssetPlacement> {};
            const auto name = placement && !placement->name.empty() ? placement->name : assetNameFromEntry(entry);
            reg.get_or_emplace<vultra::MetaComponent>(entity).name = name;
            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
            if (placement)
            {
                const auto defaultTransform = transform;
                transform = placement->transform;
                if (!options.keepPosition)
                    transform.position = defaultTransform.position;
                if (!options.keepRotation)
                    transform.rotation = defaultTransform.rotation;
                if (!options.keepScale)
                    transform.scale = defaultTransform.scale;
                transform.dirty = true;
            }

            if (entry.type == vasset::VAssetType::eMesh)
            {
                reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = uuid});
                ctx.state.statusMessage = "Created mesh entity: " + name;
            }
            else
            {
                reg.emplace<vultra::GaussianSplatComponent>(entity,
                                                            vultra::GaussianSplatComponent {.gaussianSplat = uuid});
                ctx.state.statusMessage = "Created gaussian splat entity: " + name;
            }
        }
        else
        {
            ctx.state.statusMessage = "Dropped asset type cannot be instantiated in the scene.";
            return entt::null;
        }

        if (entity == entt::null)
            return entt::null;
        if (options.beforeSibling != entt::null)
            world.insertBefore(entity, options.beforeSibling);
        else if (options.afterSibling != entt::null)
            world.insertAfter(entity, options.afterSibling);

        if (auto* id = world.registry().try_get<vultra::IDComponent>(entity))
            Selection::select(SelectionCategory::Entity, id->uuid);

        ctx.state.sceneDirty = true;
        if (ctx.history)
            ctx.history->setNextLabel(ctx.state.statusMessage.empty() ? std::string {"history.instantiateAsset"} :
                                                                        ctx.state.statusMessage);
        if (out)
        {
            *out = entityJson(world, entity);
            (*out)["assetUuid"] = uuid.toString();
            (*out)["assetType"] = vasset::toString(entry.type);
            (*out)["sourcePath"] = entry.sourcePath;
            (*out)["importedPath"] = entry.importedPath;
        }
        return entity;
    }
} // namespace vultra_app
