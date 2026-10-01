#include "script_module.hpp"

#include <vultra/api/native_plugin.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/native_script.hpp>
#include <vultra/scripting/script_host.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        struct FileStamp
        {
            std::filesystem::file_time_type modified;
            uintmax_t                       size;
            friend bool                     operator==(const FileStamp&, const FileStamp&) = default;
        };

        std::optional<FileStamp> fileStamp(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto      modified = std::filesystem::last_write_time(path, error);
            if (error)
            {
                return std::nullopt;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error)
            {
                return std::nullopt;
            }
            return FileStamp {modified, size};
        }

        class StagedModule
        {
        public:
            StagedModule(const std::filesystem::path& source, bool stage)
            {
                if (!stage)
                {
                    m_Path = source;
                    return;
                }
                m_Path = source.parent_path() / (source.stem().string() + "-vultra-" + StableId::generate().toString() +
                                                 source.extension().string());
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

            ~StagedModule()
            {
                if (m_Owned)
                {
                    std::error_code error;
                    std::filesystem::remove(m_Path, error);
                }
            }

            const std::filesystem::path& path() const
            {
                return m_Path;
            }

        private:
            std::filesystem::path m_Path;
            bool                  m_Owned = false;
        };

        class NativeScript final : public ScriptInstance
        {
        public:
            NativeScript(const std::filesystem::path& path,
                         SceneTree&                   scene,
                         bool                         hotReload,
                         ObjectId                     node,
                         std::string_view             typeName) :
                m_Stage(path, hotReload),
                m_TypeName(typeName),
                m_NodeInit {node.value, m_TypeName.data(), m_TypeName.size()},
                m_Plugin(m_Stage.path(), &scene, &m_NodeInit)
            {
            }

            void update(float deltaSeconds) override
            {
                m_Plugin.update(deltaSeconds);
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
            StagedModule                m_Stage;
            std::string                 m_TypeName;
            scripting::NativeScriptInit m_NodeInit;
            NativePlugin                m_Plugin;
        };

        class ExtensionInstance final : public ScriptInstance
        {
        public:
            ExtensionInstance(const std::filesystem::path& path, SceneTree& scene, bool hotReload) :
                m_Stage(path, hotReload),
                m_Registration {VULTRA_ABI_VERSION, sizeof(VultraScriptRegistrationApi), this, &registerClass},
                m_Plugin(m_Stage.path(), &scene, nullptr, &m_Registration)
            {
            }

            bool hasClass(std::string_view name) const
            {
                return std::ranges::find(m_Classes, name) != m_Classes.end();
            }

            const std::vector<std::string>& classes() const
            {
                return m_Classes;
            }

            VultraPluginInit entry() const
            {
                return m_Plugin.entry();
            }

            void update(float deltaSeconds) override
            {
                m_Plugin.update(deltaSeconds);
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
            static VultraStatus registerClass(void* userData, const char* name, uint64_t size)
            {
                if (!userData || !name || size == 0)
                {
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                auto&                  extension = *static_cast<ExtensionInstance*>(userData);
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

            StagedModule                m_Stage;
            std::vector<std::string>    m_Classes;
            VultraScriptRegistrationApi m_Registration;
            NativePlugin                m_Plugin;
        };

        class ProvidedNativeScript final : public ScriptInstance
        {
        public:
            ProvidedNativeScript(VultraPluginInit entry, SceneTree& scene, ObjectId node, std::string_view typeName) :
                m_TypeName(typeName),
                m_NodeInit {node.value, m_TypeName.data(), m_TypeName.size()},
                m_Session(entry, &scene, &m_NodeInit)
            {
            }

            void update(float deltaSeconds) override
            {
                m_Session.update(deltaSeconds);
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

        ObjectId scriptNode(const ScriptModule& module, SceneTree& scene)
        {
            const auto* node = module.node ? scene.find(NodeId {*module.node}) : &scene.root();
            if (!node)
            {
                throw std::invalid_argument("Script target node is missing: " + module.node->toString());
            }
            return node->id();
        }

        std::unique_ptr<ScriptInstance>
        loadScript(const ScriptModule& module, const std::filesystem::path& path, SceneTree& scene, bool hotReload)
        {
            const auto node = scriptNode(module, scene);
            switch (module.language)
            {
                case ScriptModule::Language::eNative:
                    return std::make_unique<NativeScript>(path, scene, hotReload, node, module.typeName);
                case ScriptModule::Language::eLua:
                    return loadLuaScript(path, scene, node);
                case ScriptModule::Language::eCSharp: {
                    if (module.typeName.empty() || !module.node)
                    {
                        throw std::invalid_argument("C# script requires a class name and scene node ID");
                    }
                    return loadDotNetScript(path, scene, node, module.typeName);
                }
            }
            throw std::invalid_argument("Unknown script language");
        }
    } // namespace

    struct ScriptHost::Impl
    {
        struct Slot
        {
            std::optional<ScriptModule>     module;
            std::optional<size_t>           provider;
            std::filesystem::path           path;
            FileStamp                       loaded;
            std::optional<FileStamp>        attempted;
            std::unique_ptr<ScriptInstance> instance;
        };

        Impl(SceneTree& scene, bool hotReload) :
            scene(scene),
            hotReload(hotReload)
        {
        }

        void replaceExtension(size_t index, const FileStamp& stamp)
        {
            auto& slot        = scripts[index];
            auto  replacement = std::make_unique<ExtensionInstance>(slot.path, scene, hotReload);
            for (size_t otherIndex = 0; otherIndex < scripts.size(); ++otherIndex)
            {
                const auto& other = scripts[otherIndex];
                if (otherIndex == index || other.module)
                {
                    continue;
                }
                const auto& existing = *static_cast<const ExtensionInstance*>(other.instance.get());
                for (const auto& name : replacement->classes())
                {
                    if (existing.hasClass(name))
                    {
                        throw std::invalid_argument("Duplicate extension script class: " + name);
                    }
                }
            }
            std::vector<std::pair<size_t, std::unique_ptr<ScriptInstance>>> dependents;
            for (size_t childIndex = index + 1; childIndex < scripts.size(); ++childIndex)
            {
                const auto& child = scripts[childIndex];
                if (child.provider != index)
                {
                    continue;
                }
                if (!replacement->hasClass(child.module->typeName))
                {
                    throw std::invalid_argument("Extension no longer registers script class: " +
                                                child.module->typeName);
                }
                dependents.emplace_back(childIndex,
                                        std::make_unique<ProvidedNativeScript>(replacement->entry(),
                                                                               scene,
                                                                               scriptNode(*child.module, scene),
                                                                               child.module->typeName));
            }

            // Stage all new instances first; retire dependent callbacks before unloading their provider library.
            for (const auto& dependent : dependents)
            {
                auto& oldInstance = scripts[dependent.first].instance;
                oldInstance->stop();
                oldInstance.reset();
            }
            slot.instance->stop();
            slot.instance = std::move(replacement);
            for (auto& [childIndex, instance] : dependents)
            {
                auto& child    = scripts[childIndex];
                child.instance = std::move(instance);
                child.loaded   = stamp;
                child.attempted.reset();
            }
        }

        SceneTree&            scene;
        std::vector<Slot>     scripts;
        std::string           reloadError;
        std::filesystem::path reloadErrorPath;
        bool                  hotReload = false;
        bool                  stopped   = false;
    };

    ScriptHost::ScriptHost(SceneTree& scene, bool hotReload) :
        m_Impl(std::make_unique<Impl>(scene, hotReload))
    {
    }

    ScriptHost::~ScriptHost()
    {
        stop();
    }

    void ScriptHost::addExtension(const std::filesystem::path& path)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Add extension to stopped host");
        }
        const auto stamp = fileStamp(path);
        if (!stamp)
        {
            throw std::runtime_error("Extension file is missing: " + path.string());
        }
        auto instance = std::make_unique<ExtensionInstance>(path, m_Impl->scene, m_Impl->hotReload);
        for (const auto& slot : m_Impl->scripts)
        {
            if (slot.module)
            {
                continue;
            }
            if (slot.path == path)
            {
                throw std::invalid_argument("Duplicate project extension path: " + path.string());
            }
            const auto& existing = *static_cast<const ExtensionInstance*>(slot.instance.get());
            for (const auto& name : instance->classes())
            {
                if (existing.hasClass(name))
                {
                    throw std::invalid_argument("Duplicate extension script class: " + name);
                }
            }
        }
        m_Impl->scripts.push_back({std::nullopt, std::nullopt, path, *stamp, std::nullopt, std::move(instance)});
    }

    void ScriptHost::add(const ScriptModule& module, const std::filesystem::path& path)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Add script to stopped host");
        }
        const auto stamp = fileStamp(path);
        if (!stamp)
        {
            throw std::runtime_error("Script file is missing: " + path.string());
        }
        std::optional<size_t>           provider;
        std::unique_ptr<ScriptInstance> instance;
        if (module.language == ScriptModule::Language::eNative && !module.typeName.empty())
        {
            for (size_t index = 0; index < m_Impl->scripts.size(); ++index)
            {
                const auto& slot = m_Impl->scripts[index];
                if (slot.module || slot.path != path)
                {
                    continue;
                }
                const auto& extension = *static_cast<const ExtensionInstance*>(slot.instance.get());
                if (!extension.hasClass(module.typeName))
                {
                    throw std::invalid_argument("Extension does not register script class: " + module.typeName);
                }
                provider = index;
                instance = std::make_unique<ProvidedNativeScript>(extension.entry(),
                                                                  m_Impl->scene,
                                                                  scriptNode(module, m_Impl->scene),
                                                                  module.typeName);
                break;
            }
        }
        if (!instance)
        {
            instance = loadScript(module, path, m_Impl->scene, m_Impl->hotReload);
        }
        m_Impl->scripts.push_back({module, provider, path, *stamp, std::nullopt, std::move(instance)});
    }

    size_t ScriptHost::reloadChanged(bool retryFailed)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Reload stopped script host");
        }
        if (!m_Impl->hotReload)
        {
            throw std::logic_error("Reload requires a hot-reload script host");
        }
        size_t count = 0;
        for (size_t index = 0; index < m_Impl->scripts.size(); ++index)
        {
            auto& slot = m_Impl->scripts[index];
            if (slot.provider)
            {
                continue;
            }
            const auto stamp = fileStamp(slot.path);
            if (!stamp || *stamp == slot.loaded || (!retryFailed && slot.attempted && *stamp == *slot.attempted))
            {
                continue;
            }
            slot.attempted = stamp;
            try
            {
                if (slot.module)
                {
                    auto replacement = loadScript(*slot.module, slot.path, m_Impl->scene, m_Impl->hotReload);
                    slot.instance->stop();
                    slot.instance = std::move(replacement);
                }
                else
                {
                    m_Impl->replaceExtension(index, *stamp);
                }
                slot.loaded = *stamp;
                if (m_Impl->reloadErrorPath == slot.path)
                {
                    m_Impl->reloadError.clear();
                    m_Impl->reloadErrorPath.clear();
                }
                Logger::app().info("Reloaded module: {}", slot.path.string());
                ++count;
            }
            catch (const std::exception& error)
            {
                m_Impl->reloadErrorPath = slot.path;
                m_Impl->reloadError     = "Reload " + slot.path.string() + ": " + error.what();
                Logger::app().error("{}", m_Impl->reloadError);
            }
        }
        return count;
    }

    std::string_view ScriptHost::lastReloadError() const
    {
        return m_Impl->reloadError;
    }

    void ScriptHost::update(float deltaSeconds)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Update stopped script host");
        }
        if (m_Impl->hotReload)
        {
            reloadChanged();
        }
        for (const auto& slot : m_Impl->scripts)
        {
            slot.instance->update(deltaSeconds);
        }
    }

    void ScriptHost::gui(EditorGui& gui)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Draw stopped script host");
        }
        for (const auto& slot : m_Impl->scripts)
        {
            slot.instance->gui(gui);
        }
    }

    void ScriptHost::stop() noexcept
    {
        if (m_Impl->stopped)
        {
            return;
        }
        m_Impl->stopped = true;
        for (auto it = m_Impl->scripts.rbegin(); it != m_Impl->scripts.rend(); ++it)
        {
            it->instance->stop();
        }
        m_Impl->scripts.clear();
    }
} // namespace vultra
