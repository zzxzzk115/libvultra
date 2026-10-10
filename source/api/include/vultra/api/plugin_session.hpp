#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/api/scene_bridge.hpp>

namespace vultra
{
    class EditorGui;
    class SceneTree;
    class ProjectManifest;

    // Owns callbacks from one module; the module itself must outlive this session.
    class PluginSession
    {
    public:
        // initData is borrowed only by initialize; the plugin must replace user_data before returning.
        PluginSession(VultraPluginInit                   initialize,
                      SceneTree*                         scene,
                      void*                              initData = nullptr,
                      const VultraScriptRegistrationApi* scripts  = nullptr,
                      const ProjectManifest*             project  = nullptr,
                      const VultraResearchApi*           research = nullptr);
        ~PluginSession();
        PluginSession(const PluginSession&)            = delete;
        PluginSession& operator=(const PluginSession&) = delete;

        void update(float deltaSeconds);
        void gui(EditorGui& gui);
        void stop() noexcept;
        bool active() const;

    private:
        VultraPluginApi m_Api {};
        SceneAccess     m_SceneAccess;
        bool            m_Active = false;
    };
} // namespace vultra
