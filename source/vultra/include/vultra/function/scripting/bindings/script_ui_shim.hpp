#pragma once

// The `ui` area is registered via a raw hook: the Ui/UiButton/UiToggle/UiSlider/
// UiProgressBar usertypes and the UI namespace use a hand-written signal
// connect/dispatch machinery (global connection registry, event polling) that is
// the documented non-generatable case. The IR pipeline owns the registrar shell
// (script_ui_binding.gen.cpp calls this hook); the body is in script_ui_shim.cpp.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    VBIND_RAW(area = ui)
    void uiRegisterBindings(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
