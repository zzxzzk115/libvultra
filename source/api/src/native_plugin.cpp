#include <vultra/api/native_plugin.hpp>

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vultra
{
    namespace
    {
        void* openModule(const std::filesystem::path& path)
        {
#ifdef _WIN32
            return LoadLibraryW(path.c_str());
#else
            return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        }

        void closeModule(void* module)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(module));
#else
            dlclose(module);
#endif
        }

        VultraPluginInit pluginEntry(void* module)
        {
#ifdef _WIN32
            return reinterpret_cast<VultraPluginInit>(
                GetProcAddress(static_cast<HMODULE>(module), "vultra_plugin_init"));
#else
            return reinterpret_cast<VultraPluginInit>(dlsym(module, "vultra_plugin_init"));
#endif
        }

        std::string moduleError()
        {
#ifdef _WIN32
            return "Windows loader error " + std::to_string(GetLastError());
#else
            const char* message = dlerror();
            return message ? message : "unknown dynamic loader error";
#endif
        }
    } // namespace

    NativePlugin::NativePlugin(const std::filesystem::path&       path,
                               SceneTree*                         scene,
                               void*                              initData,
                               const VultraScriptRegistrationApi* scripts,
                               const ProjectManifest*             project,
                               const VultraResearchApi*           research)
    {
        void* module = openModule(path);
        if (!module)
        {
            throw std::runtime_error("Load native plugin " + path.string() + ": " + moduleError());
        }
        try
        {
            const auto initialize = pluginEntry(module);
            if (!initialize)
            {
                throw std::runtime_error("Find vultra_plugin_init in " + path.string() + ": " + moduleError());
            }
            m_Session = std::make_unique<PluginSession>(initialize, scene, initData, scripts, project, research);
            m_Module  = module;
            m_Entry   = initialize;
        }
        catch (...)
        {
            closeModule(module);
            throw;
        }
    }

    NativePlugin::~NativePlugin()
    {
        m_Session.reset();
        if (m_Module)
        {
            closeModule(m_Module);
        }
    }

    void NativePlugin::update(float deltaSeconds)
    {
        m_Session->update(deltaSeconds);
    }

    void NativePlugin::gui(EditorGui& gui)
    {
        m_Session->gui(gui);
    }

    void NativePlugin::stop() noexcept
    {
        m_Session->stop();
    }

    bool NativePlugin::active() const
    {
        return m_Session->active();
    }

    VultraPluginInit NativePlugin::entry() const
    {
        return m_Entry;
    }
} // namespace vultra
