#pragma once

#include <sol/sol.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    // A Lua-scripted editor panel. `id` is the stable identity (also the
    // imgui "###id", so docking layout survives restarts); `titleKey` wins
    // over `title` for the visible label when set (i18n).
    struct ScriptedEditorPanelDesc
    {
        std::string id;
        std::string title;
        std::string titleKey;
        std::string icon;
        bool        defaultOpen {true};
        std::string owner; // plugin id; filled by the plugin system

        sol::protected_function onDraw;   // required; runs inside Begin/End
        sol::protected_function onClosed; // optional
    };

    // A Lua-scripted menu item, rendered under the editor's Tools menu.
    // `path` is "/"-separated; intermediate segments become nested submenus
    // ("My Plugin/Rescan" -> Tools > My Plugin > Rescan).
    struct ScriptedEditorMenuItemDesc
    {
        std::string id;
        std::string path;
        std::string shortcut; // display only
        std::string owner;

        sol::protected_function onClick;     // required
        sol::protected_function enabledWhen; // optional; falsy return disables
    };

    // Engine-side registry bridging Lua plugins/scripts and the editor UI.
    // The Editor Lua table is always bound; in runtimes without an editor the
    // registrations are simply never consumed. The editor drains
    // takeAddedPanels/takeRemovedPanelIds once per frame and renders
    // menuItems() in its top bar.
    class IEditorExtensionService
    {
    public:
        SERVICE_REGISTER(IEditorExtensionService);

        virtual ~IEditorExtensionService() = default;

        // script-facing; returns false + sets `error` on duplicate id etc.
        virtual bool registerPanel(ScriptedEditorPanelDesc desc, std::string& error)       = 0;
        virtual bool registerMenuItem(ScriptedEditorMenuItemDesc desc, std::string& error) = 0;
        virtual bool unregister(std::string_view id)                                       = 0;

        // plugin-system bookkeeping: registrations made while an owner is set
        // are tagged and torn down with unregisterOwned on plugin unload
        virtual void        setCurrentOwner(std::string_view pluginId) = 0;
        virtual std::size_t unregisterOwned(std::string_view pluginId) = 0;

        // editor-side consumption
        virtual std::vector<ScriptedEditorPanelDesc>          takeAddedPanels()     = 0;
        virtual std::vector<std::string>                      takeRemovedPanelIds() = 0;
        virtual const std::vector<ScriptedEditorMenuItemDesc>& menuItems() const    = 0;
    };
} // namespace vultra
