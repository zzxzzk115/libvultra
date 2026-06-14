#include "vultra/function/scripting/bindings/script_scene_shim.hpp"

#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/services/scene_service.hpp"
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
} // namespace vultra
