#include "native_script_module.hpp"

#include <vultra/scripting/native_script.hpp>

#include <algorithm>
#include <exception>

namespace vultra
{
    namespace
    {
        class NativeScript final : public ScriptInstance
        {
        public:
            NativeScript(const std::filesystem::path& path,
                         SceneTree&                   scene,
                         bool                         hotReload,
                         ObjectId                     node,
                         std::string_view             typeName,
                         const ProjectManifest*       project) :
                m_Stage(path, hotReload),
                m_TypeName(typeName),
                m_NodeInit {node.value, m_TypeName.data(), m_TypeName.size()},
                m_Plugin(m_Stage.path(), &scene, &m_NodeInit, nullptr, project)
            {
            }

            void update(float deltaSeconds) override
            {
                m_Plugin.update(deltaSeconds);
            }

            void notify(VultraSceneEvent event, float deltaSeconds) override
            {
                m_Plugin.notify(event, deltaSeconds);
            }

            void gui(EditorGui& gui) override
            {
                m_Plugin.gui(gui);
            }

            void stop() noexcept override
            {
                m_Plugin.stop();
            }

        private:
            StagedNativeModule          m_Stage;
            std::string                 m_TypeName;
            scripting::NativeScriptInit m_NodeInit;
            NativePlugin                m_Plugin;
        };

        class ProvidedNativeScript final : public ScriptInstance
        {
        public:
            ProvidedNativeScript(VultraPluginInit       entry,
                                 SceneTree&             scene,
                                 ObjectId               node,
                                 std::string_view       typeName,
                                 const ProjectManifest* project) :
                m_TypeName(typeName),
                m_NodeInit {node.value, m_TypeName.data(), m_TypeName.size()},
                m_Session(entry, &scene, &m_NodeInit, nullptr, project)
            {
            }

            void update(float deltaSeconds) override
            {
                m_Session.update(deltaSeconds);
            }

            void notify(VultraSceneEvent event, float deltaSeconds) override
            {
                m_Session.notify(event, deltaSeconds);
            }

            void gui(EditorGui& gui) override
            {
                m_Session.gui(gui);
            }

            void stop() noexcept override
            {
                m_Session.stop();
            }

        private:
            std::string                 m_TypeName;
            scripting::NativeScriptInit m_NodeInit;
            PluginSession               m_Session;
        };

    } // namespace

    StagedNativeModule::StagedNativeModule(const std::filesystem::path& source, bool stage)
    {
        if (!stage)
        {
            m_Path = source;
            return;
        }
        m_Path = source.parent_path() /
                 (source.stem().string() + "-vultra-" + StableId::generate().toString() + source.extension().string());
        try
        {
            std::filesystem::copy_file(source, m_Path);
            m_Owned = true;
        }
        catch (...)
        {
            std::error_code error;
            std::filesystem::remove(m_Path, error);
            throw;
        }
    }

    StagedNativeModule::~StagedNativeModule()
    {
        if (m_Owned)
        {
            std::error_code error;
            std::filesystem::remove(m_Path, error);
        }
    }

    const std::filesystem::path& StagedNativeModule::path() const
    {
        return m_Path;
    }

    NativeExtension::NativeExtension(const std::filesystem::path& path,
                                     SceneTree&                   scene,
                                     bool                         hotReload,
                                     const ProjectManifest*       project) :
        m_Stage(path, hotReload),
        m_Registration {VULTRA_ABI_VERSION, sizeof(VultraScriptRegistrationApi), this, &registerClass},
        m_Plugin(m_Stage.path(), &scene, nullptr, &m_Registration, project)
    {
    }

    bool NativeExtension::hasClass(std::string_view name) const
    {
        return std::ranges::find(m_Classes, name) != m_Classes.end();
    }

    const std::vector<std::string>& NativeExtension::classes() const
    {
        return m_Classes;
    }

    VultraPluginInit NativeExtension::entry() const
    {
        return m_Plugin.entry();
    }

    void NativeExtension::update(float deltaSeconds)
    {
        m_Plugin.update(deltaSeconds);
    }

    void NativeExtension::notify(VultraSceneEvent event, float deltaSeconds)
    {
        m_Plugin.notify(event, deltaSeconds);
    }

    void NativeExtension::gui(EditorGui& gui)
    {
        m_Plugin.gui(gui);
    }

    void NativeExtension::stop() noexcept
    {
        m_Plugin.stop();
    }

    VultraStatus NativeExtension::registerClass(void* userData, const char* name, uint64_t size)
    {
        if (!userData || !name || size == 0)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        auto&                  extension = *static_cast<NativeExtension*>(userData);
        const std::string_view className(name, size);
        if (extension.hasClass(className))
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            extension.m_Classes.emplace_back(className);
            return VULTRA_STATUS_OK;
        }
        catch (const std::exception&)
        {
            return VULTRA_STATUS_ERROR;
        }
    }

    std::unique_ptr<ScriptInstance> loadNativeScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     bool                         hotReload,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ProjectManifest*       project)
    {
        return std::make_unique<NativeScript>(path, scene, hotReload, node, typeName, project);
    }

    std::unique_ptr<ScriptInstance> loadProvidedNativeScript(VultraPluginInit       entry,
                                                             SceneTree&             scene,
                                                             ObjectId               node,
                                                             std::string_view       typeName,
                                                             const ProjectManifest* project)
    {
        return std::make_unique<ProvidedNativeScript>(entry, scene, node, typeName, project);
    }
} // namespace vultra
