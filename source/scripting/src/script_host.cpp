#include "script_module.hpp"
#ifdef VULTRA_SCRIPT_NATIVE
#include "native_script_module.hpp"
#endif

#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

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

        ObjectId scriptNode(const ScriptModule& module, SceneTree& scene)
        {
            const auto* node = module.node ? scene.find(NodeId {*module.node}) : &scene.root();
            if (!node)
            {
                throw std::invalid_argument("Script target node is missing: " + module.node->toString());
            }
            return node->id();
        }

        std::unique_ptr<ScriptInstance> loadScript(const ScriptModule&          module,
                                                   const std::filesystem::path& path,
                                                   SceneTree&                   scene,
                                                   bool                         hotReload,
                                                   const ProjectManifest*       project,
                                                   const ScriptInstance*        previous = nullptr,
                                                   const ScriptInstance*        peer     = nullptr)
        {
            const auto node = scriptNode(module, scene);
            switch (module.language)
            {
                case ScriptModule::Language::eNative:
#ifdef VULTRA_SCRIPT_NATIVE
                    return loadNativeScript(path, scene, hotReload, node, module.typeName, project);
#else
                    throw std::invalid_argument("Native scripts require the script_native feature");
#endif
                case ScriptModule::Language::eLua:
#ifdef VULTRA_SCRIPT_LUA
                    return loadLuaScript(path, scene, node, project);
#else
                    throw std::invalid_argument("Lua scripts require the script_lua feature");
#endif
                case ScriptModule::Language::eCSharp: {
#ifdef VULTRA_SCRIPT_CSHARP
                    if (module.typeName.empty() || !module.node)
                    {
                        throw std::invalid_argument("C# script requires a class name and scene node ID");
                    }
                    return loadDotNetScript(path, scene, node, module.typeName, previous, project, peer);
#else
                    throw std::invalid_argument("C# scripts require the script_csharp feature");
#endif
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

        Impl(SceneTree& scene, bool hotReload, const ProjectManifest* project) :
            scene(scene),
            project(project),
            hotReload(hotReload)
        {
        }

        void retireMissingNodes()
        {
            for (auto& slot : scripts)
            {
                if (!slot.instance || !slot.module || !slot.module->node || scene.find(NodeId {*slot.module->node}))
                {
                    continue;
                }
                slot.instance->stop();
                slot.instance.reset();
                Logger::app().info("Detached script from removed node: {}", slot.module->node->toString());
            }
        }

#ifdef VULTRA_SCRIPT_NATIVE
        void replaceExtension(size_t index, const FileStamp& stamp)
        {
            auto& slot        = scripts[index];
            auto  replacement = std::make_unique<NativeExtension>(slot.path, scene, hotReload, project);
            for (size_t otherIndex = 0; otherIndex < scripts.size(); ++otherIndex)
            {
                const auto& other = scripts[otherIndex];
                if (otherIndex == index || other.module)
                {
                    continue;
                }
                const auto& existing = *static_cast<const NativeExtension*>(other.instance.get());
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
                if (child.provider != index || !child.instance)
                {
                    continue;
                }
                if (!replacement->hasClass(child.module->typeName))
                {
                    throw std::invalid_argument("Extension no longer registers script class: " +
                                                child.module->typeName);
                }
                dependents.emplace_back(childIndex,
                                        loadProvidedNativeScript(replacement->entry(),
                                                                 scene,
                                                                 scriptNode(*child.module, scene),
                                                                 child.module->typeName,
                                                                 project));
            }

            // Stage all new instances first; retire dependent callbacks before unloading their provider library.
            for (const auto& dependent : dependents)
            {
                auto& oldInstance = scripts[dependent.first].instance;
                auto& node        = *scene.find(scriptNode(*scripts[dependent.first].module, scene));
                scene.replaceBehavior(node, *oldInstance, *dependent.second);
                oldInstance->stop();
                oldInstance.reset();
            }
            slot.instance->stop();
            slot.instance = std::move(replacement);
            for (auto& [childIndex, instance] : dependents)
            {
                auto& child    = scripts[childIndex];
                child.instance = std::move(instance);
                child.instance->notify(VULTRA_SCENE_RELOADED);
                child.loaded = stamp;
                child.attempted.reset();
            }
        }
#endif

        SceneTree&             scene;
        const ProjectManifest* project = nullptr;
        std::vector<Slot>      scripts;
        std::string            reloadError;
        std::filesystem::path  reloadErrorPath;
        bool                   hotReload = false;
        bool                   stopped   = false;
    };

    ScriptHost::ScriptHost(SceneTree& scene, bool hotReload, const ProjectManifest* project) :
        m_Impl(std::make_unique<Impl>(scene, hotReload, project))
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
#ifdef VULTRA_SCRIPT_NATIVE
        const auto stamp = fileStamp(path);
        if (!stamp)
        {
            throw std::runtime_error("Extension file is missing: " + path.string());
        }
        auto instance = std::make_unique<NativeExtension>(path, m_Impl->scene, m_Impl->hotReload, m_Impl->project);
        for (const auto& slot : m_Impl->scripts)
        {
            if (!slot.instance)
            {
                continue;
            }
            if (slot.module)
            {
                continue;
            }
            if (slot.path == path)
            {
                throw std::invalid_argument("Duplicate project extension path: " + path.string());
            }
            const auto& existing = *static_cast<const NativeExtension*>(slot.instance.get());
            for (const auto& name : instance->classes())
            {
                if (existing.hasClass(name))
                {
                    throw std::invalid_argument("Duplicate extension script class: " + name);
                }
            }
        }
        m_Impl->scripts.push_back({std::nullopt, std::nullopt, path, *stamp, std::nullopt, std::move(instance)});
#else
        throw std::invalid_argument("Native extensions require the script_native feature: " + path.string());
#endif
    }

    void ScriptHost::add(const ScriptModule& module, const std::filesystem::path& path)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Add script to stopped host");
        }
        if (m_Impl->scene.running())
        {
            throw std::logic_error("Attach scripts before starting the scene");
        }
        const auto target = module.node.value_or(m_Impl->scene.root().idInScene().value);
        for (const auto& slot : m_Impl->scripts)
        {
            if (slot.instance && slot.module &&
                slot.module->node.value_or(m_Impl->scene.root().idInScene().value) == target)
            {
                throw std::invalid_argument("A scene node can have only one script attachment");
            }
        }
        const auto stamp = fileStamp(path);
        if (!stamp)
        {
            throw std::runtime_error("Script file is missing: " + path.string());
        }
        std::optional<size_t>           provider;
        std::unique_ptr<ScriptInstance> instance;
#ifdef VULTRA_SCRIPT_NATIVE
        if (module.language == ScriptModule::Language::eNative && !module.typeName.empty())
        {
            for (size_t index = 0; index < m_Impl->scripts.size(); ++index)
            {
                const auto& slot = m_Impl->scripts[index];
                if (slot.module || slot.path != path)
                {
                    continue;
                }
                const auto& extension = *static_cast<const NativeExtension*>(slot.instance.get());
                if (!extension.hasClass(module.typeName))
                {
                    throw std::invalid_argument("Extension does not register script class: " + module.typeName);
                }
                provider = index;
                instance = loadProvidedNativeScript(extension.entry(),
                                                    m_Impl->scene,
                                                    scriptNode(module, m_Impl->scene),
                                                    module.typeName,
                                                    m_Impl->project);
                break;
            }
        }
#endif
        if (!instance)
        {
            const ScriptInstance* peer = nullptr;
            if (module.language == ScriptModule::Language::eCSharp)
            {
                for (const auto& slot : m_Impl->scripts)
                {
                    if (slot.module && slot.module->language == ScriptModule::Language::eCSharp && slot.instance)
                    {
                        peer = slot.instance.get();
                        break;
                    }
                }
            }
            instance = loadScript(module, path, m_Impl->scene, m_Impl->hotReload, m_Impl->project, nullptr, peer);
        }
        m_Impl->scripts.push_back({module, provider, path, *stamp, std::nullopt, std::move(instance)});
        auto& slot = m_Impl->scripts.back();
        try
        {
            m_Impl->scene.attachBehavior(*m_Impl->scene.find(scriptNode(module, m_Impl->scene)), *slot.instance);
        }
        catch (...)
        {
            m_Impl->scripts.pop_back();
            throw;
        }
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
        m_Impl->retireMissingNodes();
        size_t count = 0;
        for (size_t index = 0; index < m_Impl->scripts.size(); ++index)
        {
            auto& slot = m_Impl->scripts[index];
            if (slot.provider || !slot.instance)
            {
                continue;
            }
            const auto stamp = fileStamp(slot.path);
            // Restoring the last good file can also restore its timestamp after a failed reload.
            if (!stamp || (!slot.attempted && *stamp == slot.loaded) ||
                (!retryFailed && slot.attempted && *stamp == *slot.attempted))
            {
                continue;
            }
            slot.attempted = stamp;
            try
            {
                if (slot.module)
                {
                    auto  replacement = loadScript(*slot.module,
                                                   slot.path,
                                                   m_Impl->scene,
                                                   m_Impl->hotReload,
                                                   m_Impl->project,
                                                   slot.instance.get());
                    auto& node        = *m_Impl->scene.find(scriptNode(*slot.module, m_Impl->scene));
                    m_Impl->scene.replaceBehavior(node, *slot.instance, *replacement);
                    slot.instance->stop();
                    slot.instance = std::move(replacement);
                    slot.instance->notify(VULTRA_SCENE_RELOADED);
                }
                else
                {
#ifdef VULTRA_SCRIPT_NATIVE
                    m_Impl->replaceExtension(index, *stamp);
#else
                    throw std::logic_error("Native extensions require the script_native feature");
#endif
                }
                slot.loaded = *stamp;
                slot.attempted.reset();
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
        m_Impl->retireMissingNodes();
        if (m_Impl->hotReload)
        {
            reloadChanged();
        }
        m_Impl->scene.advance(deltaSeconds);
        m_Impl->retireMissingNodes();
        for (auto& slot : m_Impl->scripts)
        {
            if (!slot.instance || slot.module)
            {
                continue;
            }
            slot.instance->update(deltaSeconds);
            m_Impl->retireMissingNodes();
        }
    }

    void ScriptHost::gui(EditorGui& gui)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Draw stopped script host");
        }
#ifdef VULTRA_SCRIPT_GUI
        m_Impl->retireMissingNodes();
        for (const auto& slot : m_Impl->scripts)
        {
            if (!slot.instance)
            {
                continue;
            }
            const auto* node = slot.module ? m_Impl->scene.find(scriptNode(*slot.module, m_Impl->scene)) : nullptr;
            if (slot.instance && (!slot.module || (node && node->isInsideTree() && !node->isQueuedForDeletion())))
            {
                slot.instance->gui(gui);
            }
        }
#else
        throw std::logic_error("Script GUI requires the api_ui feature");
#endif
    }

    void ScriptHost::stop() noexcept
    {
        if (m_Impl->stopped)
        {
            return;
        }
        m_Impl->stopped = true;
        m_Impl->scene.stop();
        for (auto it = m_Impl->scripts.rbegin(); it != m_Impl->scripts.rend(); ++it)
        {
            if (it->instance)
            {
                if (it->module)
                {
                    if (auto* node =
                            it->module->node ? m_Impl->scene.find(NodeId {*it->module->node}) : &m_Impl->scene.root())
                    {
                        m_Impl->scene.detachBehavior(*node, *it->instance);
                    }
                }
                it->instance->stop();
            }
        }
        m_Impl->scripts.clear();
    }
} // namespace vultra
