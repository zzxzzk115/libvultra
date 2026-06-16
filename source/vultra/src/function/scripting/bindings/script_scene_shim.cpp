#include "vultra/function/scripting/bindings/script_scene_shim.hpp"

#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/services/scene_service.hpp"
#include "vultra/function/world/components/persistent_component.hpp"
#include "vultra/function/world/world.hpp"

namespace vultra
{
    bool sceneLoad(ScriptContext& ctx, const std::string& uri)
    {
        return ctx.sceneService ? ctx.sceneService->loadSceneSync(uri) != nullptr : false;
    }

    ScriptEntity sceneInstantiate(ScriptContext& ctx, const std::string& uri)
    {
        auto* world = ctx.world();
        if (!ctx.sceneService || !world)
            return ScriptEntity {};

        return ScriptEntity {ctx.sceneService->instantiateScene(*world, uri, entt::null, false)};
    }

    ScriptEntity sceneInstantiateChild(ScriptContext& ctx, const std::string& uri,
                                       const ScriptEntity& parent, bool clearWorld)
    {
        auto* world = ctx.world();
        if (!ctx.sceneService || !world || (parent.value != entt::null && !ctx.isValid(parent.value)))
            return ScriptEntity {};

        return ScriptEntity {ctx.sceneService->instantiateScene(*world, uri, parent.value, clearWorld)};
    }

    bool sceneSaveWorld(ScriptContext& ctx, const std::string& uri)
    {
        auto* world = ctx.world();
        return ctx.sceneService && world ? ctx.sceneService->saveWorldAsSceneSync(uri, *world, entt::null) : false;
    }

    bool sceneSaveEntity(ScriptContext& ctx, const std::string& uri, const ScriptEntity& root)
    {
        auto* world = ctx.world();
        if (!ctx.sceneService || !world || !ctx.isValid(root.value))
            return false;

        return ctx.sceneService->saveWorldAsSceneSync(uri, *world, root.value);
    }

    void sceneDontDestroyOnLoad(ScriptContext& ctx, const ScriptEntity& entity)
    {
        auto* world = ctx.world();
        if (!world || !ctx.isValid(entity.value))
            return;
        world->registry().emplace_or_replace<PersistentComponent>(entity.value);
    }

    bool sceneIsPersistent(ScriptContext& ctx, const ScriptEntity& entity)
    {
        auto* world = ctx.world();
        if (!world || !ctx.isValid(entity.value))
            return false;
        const auto* p = world->registry().try_get<PersistentComponent>(entity.value);
        return p != nullptr && p->keepOnLoad;
    }
} // namespace vultra
