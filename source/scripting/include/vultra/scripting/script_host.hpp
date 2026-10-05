#pragma once

#include <vultra/assets/project_manifest.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace vultra
{
    class EditorGui;
    class SceneTree;

    // Each module belongs to one project scene. Call update and gui in their respective frame phases.
    // The optional project is borrowed for the host lifetime and enables model selection by asset ID.
    class ScriptHost
    {
    public:
        explicit ScriptHost(SceneTree& scene, bool hotReload = false, const ProjectManifest* project = nullptr);
        ~ScriptHost();
        ScriptHost(const ScriptHost&)            = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        void addExtension(const std::filesystem::path& path);
        void add(const ScriptModule& module, const std::filesystem::path& path);
        // Call only between update and GUI callbacks; failed replacements leave the old module active.
        size_t           reloadChanged(bool retryFailed = false);
        std::string_view lastReloadError() const;
        void             update(float deltaSeconds);
        void             gui(EditorGui& gui);
        void             stop() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
