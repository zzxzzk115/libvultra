#include "editor_app/prefab_ops.hpp"

#include "editor_app/selection.hpp"

#include <vultra/function/scene/vscn_document.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/world.hpp>

#include <system_error>

namespace vultra_app
{
    namespace
    {
        std::filesystem::path assetRootPath(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string pathToResUri(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto      root = assetRootPath(ctx);
            std::error_code ec;
            const auto      rel     = std::filesystem::relative(path.lexically_normal(), root, ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }
    } // namespace

    PrefabCreateResult createPrefabFromEntity(EditorContext&               ctx,
                                              vultra::World&               world,
                                              entt::entity                 root,
                                              const std::filesystem::path& savePath)
    {
        PrefabCreateResult result {};
        auto&              reg = world.registry();
        if (!reg.valid(root))
        {
            result.message = "Cannot create prefab: invalid entity.";
            return result;
        }

        auto* sceneService = ctx.services ? ctx.services->tryGet<vultra::ISceneService>() : nullptr;
        auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        if (!sceneService || !assetService)
        {
            result.message = "Cannot create prefab: scene/asset service unavailable.";
            return result;
        }

        std::filesystem::path target = savePath;
        if (target.extension() != ".vprefab")
            target.replace_extension(".vprefab");

        const std::string resUri = pathToResUri(ctx, target);
        if (resUri.empty())
        {
            result.message = "Prefab must be saved inside the project asset root.";
            return result;
        }

        std::string prefabName = target.stem().generic_string();

        // The prefab file is a plain scene: strip any instance marker so the subtree serializes in
        // full (the source entity is replaced by an instance below).
        if (reg.all_of<vultra::PrefabInstanceComponent>(root))
            reg.remove<vultra::PrefabInstanceComponent>(root);

        const entt::entity parent  = world.parent(root);
        const entt::entity nextSib = world.nextSibling(root);

        auto doc = sceneService->captureWorldAsScene(world, root);
        if (!doc.root)
        {
            result.message = "Failed to serialize entity for prefab.";
            return result;
        }

        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);

        if (!sceneService->saveSceneSync(resUri, doc))
        {
            result.message = "Failed to write prefab file.";
            return result;
        }

        // Register the new asset so res:// resolves for instancing and future loads.
        assetService->reimportAsset(resUri, false);

        // Replace the source subtree with an instance of the prefab.
        world.destroyRecursive(root);
        const entt::entity inst = sceneService->instantiatePrefab(world, resUri, parent);
        if (inst == entt::null)
        {
            result.message = "Prefab written but instancing failed.";
            result.prefabUri = resUri;
            return result;
        }

        if (nextSib != entt::null && reg.valid(nextSib))
            world.insertBefore(inst, nextSib);

        if (auto* id = reg.try_get<vultra::IDComponent>(inst))
            Selection::select(SelectionCategory::Entity, id->uuid);

        ++ctx.state.assetFileGeneration;

        result.success      = true;
        result.instanceRoot = inst;
        result.prefabUri    = resUri;
        result.message      = "Created prefab: " + prefabName;
        return result;
    }

    bool unpackPrefab(EditorContext& ctx, vultra::World& world, entt::entity instanceRoot, std::string& message)
    {
        (void)ctx;
        auto& reg = world.registry();
        if (!reg.valid(instanceRoot) || !reg.all_of<vultra::PrefabInstanceComponent>(instanceRoot))
        {
            message = "Selected entity is not a prefab instance.";
            return false;
        }

        // Bake into plain entities: drop the instance marker. Current component values are kept, so
        // the subtree round-trips as authored entities from now on.
        reg.remove<vultra::PrefabInstanceComponent>(instanceRoot);
        message = "Unpacked prefab instance.";
        return true;
    }
} // namespace vultra_app
