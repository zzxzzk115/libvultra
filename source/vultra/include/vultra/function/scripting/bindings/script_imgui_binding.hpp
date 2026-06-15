#pragma once

#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    namespace imgui_lua_detail
    {
        // Raises a Lua error (via C++ exception) when called outside the
        // ImGui frame (no context or not between NewFrame/EndFrame).
        void ensureFrame();
    } // namespace imgui_lua_detail

    // Implemented by script_imgui_binding.gen.cpp (tools/python/gen_imgui_lua.py).
    void registerGeneratedImGuiBindings(sol::state& lua);

    // Registers the ImGui table only when an IImGuiService is present
    // (editor / dev builds); headless runtimes get no ImGui global.
    // Irregular functions (InputText) are hand-written here on top of the
    // generated subset.
    void registerScriptImGuiBindings(sol::state& lua, ScriptContext& ctx);

    // Registers the imgui-ext extension tables (ImGuizmo, ImOGuizmo, ...) onto
    // the Lua state. Hand-written (these libraries have no dear_bindings
    // metadata). Called from registerScriptImGuiBindings after the ImGui table.
    // Extension tables keep their upstream PascalCase names, like ImGui.
    void registerImGuiExtBindings(sol::state& lua);
} // namespace vultra
