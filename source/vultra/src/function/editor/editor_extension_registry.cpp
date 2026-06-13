#include "vultra/function/editor/editor_extension_registry.hpp"

#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <utility>

namespace vultra
{
    bool EditorExtensionRegistry::registerPanel(ScriptedEditorPanelDesc desc, std::string& error)
    {
        if (desc.id.empty())
        {
            error = "panel id must not be empty";
            return false;
        }
        if (!desc.onDraw.valid())
        {
            error = "panel '" + desc.id + "' requires an onDraw function";
            return false;
        }
        if (m_Registered.find(desc.id) != m_Registered.end())
        {
            error = "id '" + desc.id + "' is already registered";
            return false;
        }

        desc.owner = m_CurrentOwner;
        m_Registered.emplace(desc.id, Entry {Kind::ePanel, m_CurrentOwner});
        m_PendingPanels.push_back(std::move(desc));
        return true;
    }

    bool EditorExtensionRegistry::registerMenuItem(ScriptedEditorMenuItemDesc desc, std::string& error)
    {
        if (desc.id.empty())
        {
            error = "menu item id must not be empty";
            return false;
        }
        if (desc.path.empty())
        {
            error = "menu item '" + desc.id + "' requires a path";
            return false;
        }
        if (!desc.onClick.valid())
        {
            error = "menu item '" + desc.id + "' requires an onClick function";
            return false;
        }
        if (m_Registered.find(desc.id) != m_Registered.end())
        {
            error = "id '" + desc.id + "' is already registered";
            return false;
        }

        desc.owner = m_CurrentOwner;
        m_Registered.emplace(desc.id, Entry {Kind::eMenuItem, m_CurrentOwner});
        m_MenuItems.push_back(std::move(desc));
        return true;
    }

    bool EditorExtensionRegistry::unregister(std::string_view id)
    {
        auto it = m_Registered.find(std::string {id});
        if (it == m_Registered.end())
            return false;

        const Kind kind = it->second.kind;
        m_Registered.erase(it);

        if (kind == Kind::ePanel)
        {
            // drop a still-pending add for this id, and tell the editor to
            // remove a panel it may already have materialized
            std::erase_if(m_PendingPanels, [&](const ScriptedEditorPanelDesc& p) { return p.id == id; });
            m_RemovedPanelIds.emplace_back(id);
        }
        else
        {
            std::erase_if(m_MenuItems, [&](const ScriptedEditorMenuItemDesc& m) { return m.id == id; });
        }
        return true;
    }

    void EditorExtensionRegistry::setCurrentOwner(std::string_view pluginId)
    {
        m_CurrentOwner.assign(pluginId);
    }

    std::size_t EditorExtensionRegistry::unregisterOwned(std::string_view pluginId)
    {
        std::size_t removed = 0;
        for (auto it = m_Registered.begin(); it != m_Registered.end();)
        {
            if (it->second.owner != pluginId)
            {
                ++it;
                continue;
            }
            const std::string id   = it->first;
            const Kind        kind = it->second.kind;
            it                     = m_Registered.erase(it);
            ++removed;

            if (kind == Kind::ePanel)
            {
                std::erase_if(m_PendingPanels, [&](const ScriptedEditorPanelDesc& p) { return p.id == id; });
                m_RemovedPanelIds.push_back(id);
            }
            else
            {
                std::erase_if(m_MenuItems, [&](const ScriptedEditorMenuItemDesc& m) { return m.id == id; });
            }
        }
        if (removed > 0)
            VULTRA_CORE_INFO("[EditorExtensions] Unregistered {} extension(s) owned by '{}'", removed, pluginId);
        return removed;
    }

    std::vector<ScriptedEditorPanelDesc> EditorExtensionRegistry::takeAddedPanels()
    {
        return std::exchange(m_PendingPanels, {});
    }

    std::vector<std::string> EditorExtensionRegistry::takeRemovedPanelIds()
    {
        return std::exchange(m_RemovedPanelIds, {});
    }

    const std::vector<ScriptedEditorMenuItemDesc>& EditorExtensionRegistry::menuItems() const { return m_MenuItems; }
} // namespace vultra
