#include "vultra/function/scripting/bindings/script_input_binding.hpp"

#include "vultra/core/services/input_service.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"

namespace vultra
{
    namespace
    {
        ScriptVec2 toScriptVec2(const glm::vec2& v) { return {v.x, v.y}; }
    } // namespace

    void registerScriptInputBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto input = script_binding::getOrCreateTable(lua, "Input");

        input.set_function("getKey", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKey(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("getKeyDown", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKeyDown(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("getKeyUp", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKeyUp(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("getMousePosition", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMousePosition()) : ScriptVec2 {};
        });

        script_binding::bindEnumTable<KeyCode>(lua, "KeyCode");
        script_binding::bindEnumTable<MouseCode>(lua, "MouseCode");
    }
} // namespace vultra
