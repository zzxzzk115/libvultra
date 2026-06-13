#pragma once

#include "vultra/function/services/editor_extension_service.hpp"

#include <unordered_map>

namespace vultra
{
    // Concrete IEditorExtensionService. Owned and provided by ScriptSystem
    // (registrations originate from Lua); consumed by the editor app when one
    // exists. Main-thread only.
    class EditorExtensionRegistry final : public IEditorExtensionService
    {
    public:
        bool registerPanel(ScriptedEditorPanelDesc desc, std::string& error) override;
        bool registerMenuItem(ScriptedEditorMenuItemDesc desc, std::string& error) override;
        bool unregister(std::string_view id) override;

        void        setCurrentOwner(std::string_view pluginId) override;
        std::size_t unregisterOwned(std::string_view pluginId) override;

        std::vector<ScriptedEditorPanelDesc>           takeAddedPanels() override;
        std::vector<std::string>                       takeRemovedPanelIds() override;
        const std::vector<ScriptedEditorMenuItemDesc>& menuItems() const override;

    private:
        enum class Kind
        {
            ePanel,
            eMenuItem,
        };

        struct Entry
        {
            Kind        kind;
            std::string owner;
        };

        std::unordered_map<std::string, Entry>  m_Registered; // id -> entry
        std::vector<ScriptedEditorPanelDesc>    m_PendingPanels;
        std::vector<std::string>                m_RemovedPanelIds;
        std::vector<ScriptedEditorMenuItemDesc> m_MenuItems;
        std::string                             m_CurrentOwner;
    };
} // namespace vultra
