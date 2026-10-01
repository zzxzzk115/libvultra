#include "script_module.hpp"

#include <vultra/api/plugin_session.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vultra
{
    namespace
    {
#ifdef _WIN32
#define HOST_LITERAL(text) L##text
        using HostChar = wchar_t;
#else
#define HOST_LITERAL(text) text
        using HostChar = char;
#endif
        using HostHandle         = void*;
        using InitializeRuntime  = int32_t (*)(const HostChar*, const void*, HostHandle*);
        using GetRuntimeDelegate = int32_t (*)(HostHandle, int32_t, void**);
        using CloseRuntime       = int32_t (*)(HostHandle);
        using LoadAssembly =
            int32_t (*)(const HostChar*, const HostChar*, const HostChar*, const HostChar*, void*, void**);
        constexpr int32_t kLoadAssemblyDelegate =
            5; // hostfxr_delegate_type::hdt_load_assembly_and_get_function_pointer

        class DynamicLibrary
        {
        public:
            explicit DynamicLibrary(const std::filesystem::path& path)
            {
#ifdef _WIN32
                m_Handle = LoadLibraryW(path.c_str());
#else
                m_Handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
                if (!m_Handle)
                {
                    throw std::runtime_error("Load .NET hostfxr: " + path.string());
                }
            }

            ~DynamicLibrary()
            {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(m_Handle));
#else
                dlclose(m_Handle);
#endif
            }

            DynamicLibrary(const DynamicLibrary&)            = delete;
            DynamicLibrary& operator=(const DynamicLibrary&) = delete;

            template<typename Function>
            Function symbol(const char* name) const
            {
#ifdef _WIN32
                auto* pointer = GetProcAddress(static_cast<HMODULE>(m_Handle), name);
#else
                auto* pointer = dlsym(m_Handle, name);
#endif
                if (!pointer)
                {
                    throw std::runtime_error(std::string("Find .NET hostfxr symbol: ") + name);
                }
                return reinterpret_cast<Function>(pointer);
            }

        private:
            void* m_Handle = nullptr;
        };

        std::filesystem::path hostfxrPath()
        {
            std::vector<std::filesystem::path> roots;
            if (const char* value = std::getenv("DOTNET_ROOT"))
            {
                roots.emplace_back(value);
            }
            else
            {
                if (const char* home = std::getenv("HOME"))
                {
                    roots.emplace_back(std::filesystem::path(home) / ".dotnet");
                }
#ifdef _WIN32
                if (const char* programFiles = std::getenv("ProgramFiles"))
                {
                    roots.emplace_back(std::filesystem::path(programFiles) / "dotnet");
                }
#else
                roots.emplace_back("/usr/share/dotnet");
                roots.emplace_back("/usr/lib/dotnet");
#endif
            }
            for (const auto& root : roots)
            {
                const auto directory = root / "host/fxr";
                if (!std::filesystem::is_directory(directory))
                {
                    continue;
                }
                std::filesystem::path     selected;
                std::tuple<int, int, int> selectedVersion {};
                for (const auto& entry : std::filesystem::directory_iterator(directory))
                {
#ifdef _WIN32
                    const auto candidate = entry.path() / "hostfxr.dll";
#elif defined(__APPLE__)
                    const auto candidate = entry.path() / "libhostfxr.dylib";
#else
                    const auto candidate = entry.path() / "libhostfxr.so";
#endif
                    const auto name  = entry.path().filename().string();
                    int        major = 0;
                    int        minor = 0;
                    int        patch = 0;
                    if (std::filesystem::is_regular_file(candidate) &&
                        std::sscanf(name.c_str(), "%d.%d.%d", &major, &minor, &patch) == 3 &&
                        (selected.empty() || std::tuple {major, minor, patch} > selectedVersion))
                    {
                        selected        = candidate;
                        selectedVersion = {major, minor, patch};
                    }
                }
                if (!selected.empty())
                {
                    return selected;
                }
            }
            throw std::runtime_error("Find .NET hostfxr; install the .NET runtime or set DOTNET_ROOT");
        }

        std::string configText(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Open .NET runtime config: " + path.string());
            }
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        class DotNetRuntime
        {
        public:
            explicit DotNetRuntime(const std::filesystem::path& config) :
                m_Host(hostfxrPath()),
                m_Config(configText(config))
            {
                const auto initialize  = m_Host.symbol<InitializeRuntime>("hostfxr_initialize_for_runtime_config");
                const auto getDelegate = m_Host.symbol<GetRuntimeDelegate>("hostfxr_get_runtime_delegate");
                const auto close       = m_Host.symbol<CloseRuntime>("hostfxr_close");
                HostHandle context     = nullptr;
                if (initialize(config.c_str(), nullptr, &context) < 0 || !context)
                {
                    throw std::runtime_error("Initialize .NET runtime from " + config.string());
                }
                void*      loader = nullptr;
                const auto status = getDelegate(context, kLoadAssemblyDelegate, &loader);
                close(context);
                if (status < 0 || !loader)
                {
                    throw std::runtime_error("Get .NET assembly loader");
                }
                m_LoadAssembly = reinterpret_cast<LoadAssembly>(loader);
            }

            void verifyConfig(const std::filesystem::path& config) const
            {
                if (configText(config) != m_Config)
                {
                    throw std::runtime_error("One .NET runtime per process; incompatible script runtime config: " +
                                             config.string());
                }
            }

            VultraPluginInit entry(const std::filesystem::path& bridge) const
            {
                void* pointer = nullptr;
                if (m_LoadAssembly(bridge.c_str(),
                                   HOST_LITERAL("Vultra.ManagedHost.Entry, Vultra.ManagedHost"),
                                   HOST_LITERAL("Initialize"),
                                   reinterpret_cast<const HostChar*>(-1),
                                   nullptr,
                                   &pointer) < 0 ||
                    !pointer)
                {
                    throw std::runtime_error("Load .NET script host entry from " + bridge.string());
                }
                return reinterpret_cast<VultraPluginInit>(pointer);
            }

        private:
            DynamicLibrary m_Host;
            std::string    m_Config;
            LoadAssembly   m_LoadAssembly = nullptr;
        };

        DotNetRuntime& dotNetRuntime(const std::filesystem::path& config)
        {
            // CoreCLR is process-wide. Keep its host loader alive while collectible script contexts come and go.
            static DotNetRuntime runtime(config);
            runtime.verifyConfig(config);
            return runtime;
        }

        struct ManagedLoadRequest
        {
            const char* path;
            uint64_t    pathSize;
            const char* typeName;
            uint64_t    typeNameSize;
            uint64_t    nodeId;
            void*       previous;
            void*       instance;
        };

        class DotNetScript final : public ScriptInstance
        {
        public:
            DotNetScript(const std::filesystem::path& path,
                         SceneTree&                   scene,
                         ObjectId                     node,
                         std::string_view             typeName,
                         const DotNetScript*          previous)
            {
                const auto bridge = std::filesystem::absolute(path).parent_path() / "Vultra.ManagedHost.dll";
                if (!std::filesystem::is_regular_file(bridge))
                {
                    throw std::runtime_error("Missing .NET script host: " + bridge.string());
                }
                const auto api = bridge.parent_path() / "Vultra.Scripting.dll";
                if (!std::filesystem::is_regular_file(api))
                {
                    throw std::runtime_error("Missing .NET scripting API: " + api.string());
                }
                const auto         config     = std::filesystem::path(bridge).replace_extension(".runtimeconfig.json");
                const auto         initialize = dotNetRuntime(config).entry(bridge);
                const auto         assembly   = std::filesystem::absolute(path).u8string();
                ManagedLoadRequest request {reinterpret_cast<const char*>(assembly.data()),
                                            assembly.size(),
                                            typeName.data(),
                                            typeName.size(),
                                            node.value,
                                            previous ? previous->m_State : nullptr,
                                            nullptr};
                m_Session = std::make_unique<PluginSession>(initialize, &scene, &request);
                m_State   = request.instance;
            }

            void update(float deltaSeconds) override
            {
                m_Session->update(deltaSeconds);
            }

            void gui(EditorGui& gui) override
            {
                m_Session->gui(gui);
            }

            void stop() noexcept override
            {
                m_Session->stop();
            }

        private:
            std::unique_ptr<PluginSession> m_Session;
            void*                          m_State = nullptr; // Borrowed handle owned by the managed session.
        };
    } // namespace

    std::unique_ptr<ScriptInstance> loadDotNetScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ScriptInstance*        previous)
    {
        return std::make_unique<DotNetScript>(path, scene, node, typeName, static_cast<const DotNetScript*>(previous));
    }
} // namespace vultra
