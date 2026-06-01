#include "vultra/function/scripting/script_binding.hpp"

#include "vultra/function/scripting/bindings/script_animation_binding.hpp"
#include "vultra/function/scripting/bindings/script_asset_binding.hpp"
#include "vultra/function/scripting/bindings/script_entity_binding.hpp"
#include "vultra/function/scripting/bindings/script_input_binding.hpp"
#include "vultra/function/scripting/bindings/script_math_binding.hpp"
#include "vultra/function/scripting/bindings/script_physics_binding.hpp"
#include "vultra/function/scripting/bindings/script_render_binding.hpp"
#include "vultra/function/scripting/bindings/script_scene_binding.hpp"
#include "vultra/function/scripting/bindings/script_script_binding.hpp"
#include "vultra/function/scripting/bindings/script_timing_binding.hpp"
#include "vultra/function/scripting/bindings/script_transform_binding.hpp"
#include "vultra/function/scripting/bindings/script_world_binding.hpp"

namespace vultra
{
    void registerScriptBindings(sol::state& lua, ScriptContext& ctx)
    {
        registerScriptMathBindings(lua);
        registerScriptTransformBindings(lua, ctx);
        registerScriptEntityBindings(lua, ctx);
        registerScriptWorldBindings(lua, ctx);
        registerScriptPhysicsBindings(lua, ctx);
        registerScriptAnimationBindings(lua, ctx);
        registerScriptInputBindings(lua, ctx);
        registerScriptTimingBindings(lua, ctx);
        registerScriptAssetBindings(lua, ctx);
        registerScriptSceneBindings(lua, ctx);
        registerScriptScriptBindings(lua, ctx);
        registerScriptRenderBindings(lua, ctx);
    }
} // namespace vultra
