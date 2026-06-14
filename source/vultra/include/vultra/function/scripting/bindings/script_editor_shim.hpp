#pragma once

// The `editor` area is registered via a raw hook: the `Editor` namespace is
// service-gated (absent unless IEditorExtensionService is present) and parses
// options tables into descriptor structs with (ok, err) multi-returns. The IR
// pipeline owns the registrar shell (script_editor_binding.gen.cpp calls this
// hook); the body is in script_editor_shim.cpp.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    VBIND_RAW(area = editor)
    void editorRegisterBindings(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
