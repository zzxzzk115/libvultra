#include <vultra/api/plugin_session.hpp>
#include <vultra/api/ui_bridge.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <stdexcept>

namespace vultra
{
    PluginSession::PluginSession(VultraPluginInit                   initialize,
                                 SceneTree*                         scene,
                                 void*                              initData,
                                 const VultraScriptRegistrationApi* scripts,
                                 const ProjectManifest*             project)
    {
        if (!initialize)
        {
            throw std::invalid_argument("Plugin entry is missing");
        }
        m_SceneAccess.scene   = scene;
        m_SceneAccess.project = project;
        const VultraHostApi host {VULTRA_ABI_VERSION, sizeof(VultraHostApi), &uiApi(), &sceneApi(), scripts};
        m_Api.version     = VULTRA_ABI_VERSION;
        m_Api.user_data   = initData;
        m_Api.struct_size = sizeof(VultraPluginApi);
        const auto status = initialize(&host, &m_Api);
        if (status != VULTRA_STATUS_OK || m_Api.version != VULTRA_ABI_VERSION ||
            m_Api.struct_size != sizeof(VultraPluginApi) || (initData && m_Api.user_data == initData) ||
            !m_Api.update || !m_Api.on_gui || !m_Api.stop)
        {
            throw std::runtime_error("Incompatible plugin ABI or callbacks");
        }
        m_Active = true;
    }

    PluginSession::~PluginSession()
    {
        stop();
    }

    void PluginSession::update(float deltaSeconds)
    {
        if (!m_Active)
        {
            throw std::logic_error("Update stopped plugin");
        }
        ++m_SceneAccess.serial;
        m_SceneAccess.active = true;
        const VultraSceneFrame frame {m_SceneAccess.scene ? &m_SceneAccess : nullptr, m_SceneAccess.serial};
        VultraStatus           status;
        try
        {
            status = m_Api.update(m_Api.user_data, frame, deltaSeconds);
        }
        catch (...)
        {
            m_SceneAccess.active = false;
            throw;
        }
        m_SceneAccess.active = false;
        if (status != VULTRA_STATUS_OK)
        {
            throw std::runtime_error("Plugin update failed");
        }
    }

    void PluginSession::gui(EditorGui& gui)
    {
        if (!m_Active)
        {
            throw std::logic_error("Draw stopped plugin");
        }
        if (!gui.frameActive())
        {
            throw std::logic_error("Plugin UI requires an active GUI frame");
        }
        if (m_Api.on_gui(m_Api.user_data, makeUiFrame(gui)) != VULTRA_STATUS_OK)
        {
            throw std::runtime_error("Plugin UI callback failed");
        }
    }

    void PluginSession::stop() noexcept
    {
        if (!m_Active)
        {
            return;
        }
        m_Active             = false;
        m_SceneAccess.active = false;
        if (m_Api.stop(m_Api.user_data) != VULTRA_STATUS_OK)
        {
            Logger::app().warn("Plugin stop callback failed");
        }
        m_Api = {};
    }

    bool PluginSession::active() const
    {
        return m_Active;
    }
} // namespace vultra
