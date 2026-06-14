#include "vultra/function/scripting/bindings/script_editor_shim.hpp"

#include "vultra/function/services/editor_extension_service.hpp"

#include <string>

namespace vultra
{
    namespace
    {
        // Reads a panel descriptor from a Lua options table:
        //   Editor.registerPanel{ id = "...", title = "...", titleKey = "...",
        //                         icon = "...", defaultOpen = true,
        //                         onDraw = fn, onClosed = fn }
        ScriptedEditorPanelDesc readPanelDesc(const sol::table& t)
        {
            ScriptedEditorPanelDesc desc;
            desc.id          = t.get_or<std::string>("id", "");
            desc.title       = t.get_or<std::string>("title", "");
            desc.titleKey    = t.get_or<std::string>("titleKey", "");
            desc.icon        = t.get_or<std::string>("icon", "");
            desc.defaultOpen = t.get_or("defaultOpen", true);
            desc.onDraw      = t.get<sol::protected_function>("onDraw");
            desc.onClosed    = t.get<sol::protected_function>("onClosed");
            return desc;
        }

        ScriptedEditorMenuItemDesc readMenuItemDesc(const sol::table& t)
        {
            ScriptedEditorMenuItemDesc desc;
            desc.id          = t.get_or<std::string>("id", "");
            desc.path        = t.get_or<std::string>("path", "");
            desc.shortcut    = t.get_or<std::string>("shortcut", "");
            desc.onClick     = t.get<sol::protected_function>("onClick");
            desc.enabledWhen = t.get<sol::protected_function>("enabledWhen");
            return desc;
        }
    } // namespace

    void editorRegisterBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto* svc = ctx.editorExtensionService;
        if (!svc)
            return; // no editor in this runtime; `Editor` global stays nil

        auto editor = lua.create_named_table("Editor");

        // Editor.registerPanel{...} -> ok, err
        editor.set_function("registerPanel", [svc](const sol::table& opts) {
            std::string error;
            const bool  ok = svc->registerPanel(readPanelDesc(opts), error);
            return std::make_tuple(ok, ok ? sol::optional<std::string> {} : sol::optional<std::string> {error});
        });

        // Editor.registerMenuItem{...} -> ok, err  (path is "/"-separated,
        // nested under Tools; e.g. "My Plugin/Rescan")
        editor.set_function("registerMenuItem", [svc](const sol::table& opts) {
            std::string error;
            const bool  ok = svc->registerMenuItem(readMenuItemDesc(opts), error);
            return std::make_tuple(ok, ok ? sol::optional<std::string> {} : sol::optional<std::string> {error});
        });

        // registerInspector is reserved: inspector drawer registration depends
        // on the editor's component-reflection drawing, which is Phase 5b.
        // Exposed now so plugins can feature-detect; returns ok=false.
        editor.set_function("registerInspector", [](const sol::table&) {
            return std::make_tuple(false,
                                   sol::optional<std::string> {"Editor.registerInspector is not implemented yet"});
        });

        // Editor.unregister(id) -> bool
        editor.set_function("unregister", [svc](const std::string& id) { return svc->unregister(id); });
    }
} // namespace vultra
