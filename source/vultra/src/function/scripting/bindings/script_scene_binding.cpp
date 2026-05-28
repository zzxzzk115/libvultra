#include "vultra/function/scripting/bindings/script_scene_binding.hpp"

#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/scene_service.hpp"
#include "vultra/function/world/world.hpp"

namespace vultra
{
    void registerScriptSceneBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto scene = script_binding::getOrCreateTable(lua, "Scene");

        scene.set_function("load", [&ctx](const std::string& uri) {
            return ctx.sceneService ? ctx.sceneService->loadSceneSync(uri) != nullptr : false;
        });

        scene.set_function("instantiate", [&ctx](const std::string& uri) {
            auto* world = ctx.world();
            if (!ctx.sceneService || !world)
                return ScriptEntity {};

            return ScriptEntity {ctx.sceneService->instantiateScene(*world, uri, entt::null, false)};
        });

        scene.set_function(
            "instantiateChild", [&ctx](const std::string& uri, const ScriptEntity& parent, bool clearWorld) {
                auto* world = ctx.world();
                if (!ctx.sceneService || !world || (parent.value != entt::null && !ctx.isValid(parent.value)))
                    return ScriptEntity {};

                return ScriptEntity {ctx.sceneService->instantiateScene(*world, uri, parent.value, clearWorld)};
            });

        scene.set_function("saveWorld", [&ctx](const std::string& uri) {
            auto* world = ctx.world();
            return ctx.sceneService && world ? ctx.sceneService->saveWorldAsSceneSync(uri, *world, entt::null) : false;
        });

        scene.set_function("saveEntity", [&ctx](const std::string& uri, const ScriptEntity& root) {
            auto* world = ctx.world();
            if (!ctx.sceneService || !world || !ctx.isValid(root.value))
                return false;

            return ctx.sceneService->saveWorldAsSceneSync(uri, *world, root.value);
        });
    }
} // namespace vultra
