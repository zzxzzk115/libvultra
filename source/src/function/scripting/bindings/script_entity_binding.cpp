#include "vultra/function/scripting/bindings/script_entity_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"

namespace vultra
{
    void registerScriptEntityBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptEntity>(
            "Entity",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptEntity& self) { return ctx.isValid(self.value); }),
            "transform",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptEntity& self) { return ScriptTransformRef {self.value}; }));
    }
} // namespace vultra
