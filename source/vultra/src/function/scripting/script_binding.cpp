#include "vultra/function/scripting/script_binding.hpp"

#include "vultra/function/scripting/bindings/script_animation_binding.hpp"
#include "vultra/function/scripting/bindings/script_asset_binding.hpp"
#include "vultra/function/scripting/bindings/script_audio_binding.hpp"
#include "vultra/function/scripting/bindings/script_coroutine_binding.hpp"
#include "vultra/function/scripting/bindings/script_deprecations.hpp"
#include "vultra/function/scripting/bindings/script_editor_binding.hpp"
#include "vultra/function/scripting/bindings/script_entity_binding.hpp"
#include "vultra/function/scripting/bindings/script_components_binding.hpp"
#include "vultra/function/scripting/bindings/script_imgui_binding.hpp"
#include "vultra/function/scripting/bindings/script_input_binding.hpp"
#include "vultra/function/scripting/bindings/script_math_binding.hpp"
#include "vultra/function/scripting/bindings/script_navigation_binding.hpp"
#include "vultra/function/scripting/bindings/script_physics_binding.hpp"
#include "vultra/function/scripting/bindings/script_render_binding.hpp"
#include "vultra/function/scripting/bindings/script_save_binding.hpp"
#include "vultra/function/scripting/bindings/script_scene_binding.hpp"
#include "vultra/function/scripting/bindings/script_script_binding.hpp"
#include "vultra/function/scripting/bindings/script_timing_binding.hpp"
#include "vultra/function/scripting/bindings/script_transform_binding.hpp"
#include "vultra/function/scripting/bindings/script_tween_binding.hpp"
#include "vultra/function/scripting/bindings/script_ui_binding.hpp"
#include "vultra/function/scripting/bindings/script_upscaler_binding.hpp"
#include "vultra/function/scripting/bindings/script_world_binding.hpp"

namespace vultra
{
    void registerScriptBindings(sol::state& lua, ScriptContext& ctx)
    {
        registerScriptMathBindings(lua, ctx);
        registerScriptTransformBindings(lua, ctx);
        registerScriptEntityBindings(lua, ctx);
        registerScriptWorldBindings(lua, ctx);
        registerScriptPhysicsBindings(lua, ctx);
        registerScriptNavigationBindings(lua, ctx);
        registerScriptSaveBindings(lua, ctx);
        registerScriptAnimationBindings(lua, ctx);
        registerScriptAudioBindings(lua, ctx);
        registerScriptInputBindings(lua, ctx);
        registerScriptTimingBindings(lua, ctx);
        registerScriptAssetBindings(lua, ctx);
        registerScriptSceneBindings(lua, ctx);
        registerScriptScriptBindings(lua, ctx);
        registerScriptRenderBindings(lua, ctx);
        registerScriptUpscalerBindings(lua, ctx);
        registerScriptUiBindings(lua, ctx);
        registerScriptComponentsBindings(lua, ctx);
        registerScriptCoroutineRuntime(lua);
        registerScriptTweenRuntime(lua);
        registerScriptImGuiBindings(lua, ctx);  // no-op without IImGuiService
        registerScriptEditorBindings(lua, ctx); // no-op without IEditorExtensionService

        // must run last: wraps old names of the bindings registered above
        registerScriptDeprecations(lua);
    }
} // namespace vultra
