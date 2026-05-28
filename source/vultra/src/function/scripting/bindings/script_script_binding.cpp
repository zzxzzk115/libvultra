#include "vultra/function/scripting/bindings/script_script_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/script_service.hpp"

namespace vultra
{
    void registerScriptScriptBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto script = script_binding::getOrCreateTable(lua, "Script");

        script.set_function("reloadAll",
                            [&ctx]() { return ctx.scriptService ? ctx.scriptService->reloadAllScripts() : false; });
        script.set_function("reloadEntity", [&ctx](const ScriptEntity& entity) {
            return ctx.scriptService && ctx.isValid(entity.value) ?
                       ctx.scriptService->reloadEntityScript(entity.value) :
                       false;
        });
        script.set_function("hasInstance", [&ctx](const ScriptEntity& entity) {
            return ctx.scriptService && ctx.isValid(entity.value) ? ctx.scriptService->hasScriptInstance(entity.value) :
                                                                    false;
        });
        script.set_function("destroyInstance", [&ctx](const ScriptEntity& entity) {
            if (ctx.scriptService && ctx.isValid(entity.value))
                ctx.scriptService->destroyScriptInstance(entity.value);
        });
        script.set_function("setPlaybackState", [&ctx](bool playing, bool paused) {
            if (ctx.scriptService)
                ctx.scriptService->setPlaybackState(playing, paused);
        });
        script.set_function("isPlaybackPlaying",
                            [&ctx]() { return ctx.scriptService ? ctx.scriptService->isPlaybackPlaying() : false; });
        script.set_function("isPlaybackPaused",
                            [&ctx]() { return ctx.scriptService ? ctx.scriptService->isPlaybackPaused() : false; });
        script.set_function("runString", [&ctx](const std::string& code) {
            return ctx.scriptService ? ctx.scriptService->runString(code) : false;
        });
    }
} // namespace vultra
