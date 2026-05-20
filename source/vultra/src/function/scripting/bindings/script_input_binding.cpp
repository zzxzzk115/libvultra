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

        input.set_function("getKeyRepeat", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKeyRepeat(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("getMouseButton", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->getMouseButton(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("getMouseButtonDown", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->getMouseButtonDown(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("getMouseButtonUp", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->getMouseButtonUp(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("getMouseButtonClicks", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->getMouseButtonClicks(static_cast<MouseCode>(button)) : 0;
        });

        input.set_function("getMousePosition", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMousePosition()) : ScriptVec2 {};
        });

        input.set_function("getMousePositionFlipY", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMousePositionFlipY()) : ScriptVec2 {};
        });

        input.set_function("getMousePositionDelta", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMousePositionDelta()) : ScriptVec2 {};
        });

        input.set_function("getMouseScrollDelta", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMouseScrollDelta()) : ScriptVec2 {};
        });

        script_binding::bindEnumTable<KeyCode>(lua, "KeyCode");
        script_binding::bindEnumTable<MouseCode>(lua, "MouseCode");
    }
} // namespace vultra
