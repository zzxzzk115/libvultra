#pragma once

#include <vultra/api/plugin_session.hpp>

#include <filesystem>
#include <memory>

namespace vultra
{
    class EditorGui;
    class SceneTree;
    class ProjectManifest;

    // A loaded plugin belongs to one host context. Stop callbacks before unloading its module.
    class NativePlugin
    {
    public:
        explicit NativePlugin(const std::filesystem::path&       path,
                              SceneTree*                         scene    = nullptr,
                              void*                              initData = nullptr,
                              const VultraScriptRegistrationApi* scripts  = nullptr,
                              const ProjectManifest*             project  = nullptr);
        ~NativePlugin();
        NativePlugin(const NativePlugin&)            = delete;
        NativePlugin& operator=(const NativePlugin&) = delete;

        void update(float deltaSeconds);
        void gui(EditorGui& gui);
        void stop() noexcept;

        bool             active() const;
        VultraPluginInit entry() const;

    private:
        void*                          m_Module = nullptr;
        VultraPluginInit               m_Entry  = nullptr;
        std::unique_ptr<PluginSession> m_Session;
    };
} // namespace vultra
