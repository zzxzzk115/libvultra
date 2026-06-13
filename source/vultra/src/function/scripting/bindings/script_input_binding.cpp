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

        // Canonical names follow doc/lua_api_design.md: boolean queries are
        // isX, value queries are nouns. The old get* family lives on as
        // deprecated aliases installed by registerScriptDeprecations.
        input.set_function("isKeyHeld", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->isKeyHeld(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("isKeyPressed", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->isKeyPressed(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("isKeyReleased", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->isKeyReleased(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("isKeyRepeated", [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->isKeyRepeated(static_cast<KeyCode>(key)) : false;
        });

        input.set_function("isMouseButtonHeld", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->isMouseButtonHeld(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("isMouseButtonPressed", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->isMouseButtonPressed(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("isMouseButtonReleased", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->isMouseButtonReleased(static_cast<MouseCode>(button)) : false;
        });

        input.set_function("mouseButtonClicks", [&ctx](int button) {
            return ctx.inputService ? ctx.inputService->mouseButtonClicks(static_cast<MouseCode>(button)) : 0;
        });

        input.set_function("mousePosition", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->mousePosition()) : ScriptVec2 {};
        });

        input.set_function("mousePositionFlipY", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->mousePositionFlipY()) : ScriptVec2 {};
        });

        input.set_function("mousePositionDelta", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->mousePositionDelta()) : ScriptVec2 {};
        });

        input.set_function("mouseScrollDelta", [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->mouseScrollDelta()) : ScriptVec2 {};
        });

        script_binding::bindEnumTable<KeyCode>(lua, "KeyCode");
        script_binding::bindEnumTable<MouseCode>(lua, "MouseCode");
    }
} // namespace vultra
