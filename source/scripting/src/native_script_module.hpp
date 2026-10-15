#pragma once

#include "script_module.hpp"

#include <vultra/api/native_plugin.hpp>

#include <string>
#include <vector>

namespace vultra
{
    // The loaded module is destroyed before its staged file is removed.
    class StagedNativeModule
    {
    public:
        StagedNativeModule(const std::filesystem::path& source, bool stage);
        ~StagedNativeModule();
        StagedNativeModule(const StagedNativeModule&)                     = delete;
        StagedNativeModule&          operator=(const StagedNativeModule&) = delete;
        const std::filesystem::path& path() const;

    private:
        std::filesystem::path m_Path;
        bool                  m_Owned = false;
    };

    class NativeExtension final : public ScriptInstance
    {
    public:
        NativeExtension(const std::filesystem::path& path,
                        SceneTree&                   scene,
                        bool                         hotReload,
                        const ProjectManifest*       project);
        bool                            hasClass(std::string_view name) const;
        const std::vector<std::string>& classes() const;
        VultraPluginInit                entry() const;
        void                            update(float deltaSeconds) override;
        void                            notify(VultraSceneEvent event, float deltaSeconds = 0) override;
        void                            gui(EditorGui& gui) override;
        void                            stop() noexcept override;

    private:
        static VultraStatus registerClass(void* userData, const char* name, uint64_t size);

        StagedNativeModule          m_Stage;
        std::vector<std::string>    m_Classes;
        VultraScriptRegistrationApi m_Registration;
        NativePlugin                m_Plugin;
    };

    std::unique_ptr<ScriptInstance> loadNativeScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     bool                         hotReload,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ProjectManifest*       project);
    std::unique_ptr<ScriptInstance> loadProvidedNativeScript(VultraPluginInit       entry,
                                                             SceneTree&             scene,
                                                             ObjectId               node,
                                                             std::string_view       typeName,
                                                             const ProjectManifest* project);
} // namespace vultra
