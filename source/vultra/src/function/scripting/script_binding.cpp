#include "vultra/function/scripting/script_binding.hpp"

#include "vultra/function/scripting/bindings/script_entity_binding.hpp"
#include "vultra/function/scripting/bindings/script_input_binding.hpp"
#include "vultra/function/scripting/bindings/script_math_binding.hpp"
#include "vultra/function/scripting/bindings/script_transform_binding.hpp"

namespace vultra
{
    void registerScriptBindings(sol::state& lua, ScriptContext& ctx)
    {
        registerScriptMathBindings(lua);
        registerScriptTransformBindings(lua, ctx);
        registerScriptEntityBindings(lua, ctx);
        registerScriptInputBindings(lua, ctx);
    }
} // namespace vultra
