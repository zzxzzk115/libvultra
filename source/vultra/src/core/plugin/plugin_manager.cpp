#include "vultra/core/plugin/plugin_manager.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/base/common_context.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
#if defined(_WIN32)
        std::string formatWin32Error(const DWORD code)
        {
            if (code == 0)
                return {};

            LPSTR message = nullptr;
            const DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                                  nullptr,
                                                  code,
                                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                  reinterpret_cast<LPSTR>(&message),
                                                  0,
                                                  nullptr);
            std::string out = "Win32 error " + std::to_string(code);
            if (length > 0 && message != nullptr)
                out += ": " + std::string(message, length);
            if (message != nullptr)
                ::LocalFree(message);
            return out;
        }

        std::string lastLoadLibraryError() { return formatWin32Error(::GetLastError()); }

        class ScopedDllDirectory final
        {
        public:
            explicit ScopedDllDirectory(const std::filesystem::path& directory)
            {
                const DWORD size = ::GetDllDirectoryA(0, nullptr);
                if (size > 0)
                {
                    m_Previous.resize(size);
                    const DWORD written = ::GetDllDirectoryA(size, m_Previous.data());
                    m_Previous.resize(written);
                }
                m_HadPrevious = !m_Previous.empty();
                ::SetDllDirectoryA(directory.string().c_str());
            }

            ~ScopedDllDirectory()
            {
                if (m_HadPrevious)
                    ::SetDllDirectoryA(m_Previous.c_str());
                else
                    ::SetDllDirectoryA(nullptr);
            }

        private:
            bool        m_HadPrevious {false};
            std::string m_Previous;
        };
#else
        std::string lastLoadLibraryError()
        {
            const char* message = ::dlerror();
            return message != nullptr ? std::string {message} : std::string {};
        }
#endif
    } // namespace

    static void* openLib(const char* path)
    {
#if defined(_WIN32)
        std::vector<std::string> errors;
        if (void* handle = ::LoadLibraryExA(path,
                                            nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
        {
            return handle;
        }
        errors.push_back("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR: " + lastLoadLibraryError());
        if (void* handle = ::LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
            return handle;
        errors.push_back("LOAD_WITH_ALTERED_SEARCH_PATH: " + lastLoadLibraryError());

        const auto directory = std::filesystem::path {path}.parent_path();
        {
            ScopedDllDirectory dllDirectory(directory);
            if (void* handle = ::LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
                return handle;
            errors.push_back("SetDllDirectory + LOAD_WITH_ALTERED_SEARCH_PATH: " + lastLoadLibraryError());
        }

        for (const auto& error : errors)
            VULTRA_CORE_ERROR("[PluginManager] Load attempt for '{}' failed: {}", path, error);
        return nullptr;
#else
        return ::dlopen(path, RTLD_NOW);
#endif
    }

    static bool closeLib(void* h)
    {
#if defined(_WIN32)
        if (h)
            return ::FreeLibrary(static_cast<HMODULE>(h)) != FALSE;
#else
        if (h)
            return ::dlclose(h) == 0;
#endif
        return true;
    }

    static void* getSym(void* h, const char* sym)
    {
#if defined(_WIN32)
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(h), sym));
#else
        return ::dlsym(h, sym);
#endif
    }

    bool PluginManager::load(const std::string& id, const std::string& path, EngineContext& ctx)
    {
        if (!id.empty() && isLoaded(id))
        {
            VULTRA_CORE_WARN("[PluginManager] Plugin '{}' is already loaded.", id);
            return true;
        }

        void* h = openLib(path.c_str());
        if (!h)
        {
            VULTRA_CORE_ERROR("[PluginManager] Load failed for '{}': {}", path, lastLoadLibraryError());
            return false;
        }

        auto create  = reinterpret_cast<CreatePluginFn>(getSym(h, "vultraCreatePlugin"));
        auto destroy = reinterpret_cast<DestroyPluginFn>(getSym(h, "vultraDestroyPlugin"));

        if (!create || !destroy)
        {
            VULTRA_CORE_ERROR("[PluginManager] '{}' is missing vultraCreatePlugin/vultraDestroyPlugin exports.", path);
            (void)closeLib(h);
            return false;
        }

        EnginePlugin* p = create();
        if (!p)
        {
            VULTRA_CORE_ERROR("[PluginManager] '{}' returned null plugin instance.", path);
            (void)closeLib(h);
            return false;
        }

        if (!p->install(ctx))
        {
            VULTRA_CORE_ERROR("[PluginManager] '{}' install() returned false.", path);
            destroy(p);
            (void)closeLib(h);
            return false;
        }

        m_Loaded.push_back(Loaded {.handle = h, .plugin = p, .destroy = destroy, .id = id, .path = path});
        return true;
    }

    bool PluginManager::isLoaded(const std::string& id) const
    {
        return std::ranges::any_of(m_Loaded, [&](const Loaded& loaded) { return loaded.id == id; });
    }

    bool PluginManager::unload(const std::string& id, EngineContext& ctx)
    {
        for (auto it = m_Loaded.rbegin(); it != m_Loaded.rend(); ++it)
        {
            if (it->id != id)
                continue;

            if (!it->uninstalled && it->plugin)
            {
                it->plugin->uninstall(ctx);
                it->uninstalled = true;
            }
            if (it->destroy && it->plugin)
                it->destroy(it->plugin);

            const auto eraseIt = std::next(it).base();
            const auto path    = eraseIt->path;
            const bool closed  = closeLib(it->handle);
            if (!closed)
            {
                VULTRA_CORE_ERROR("[PluginManager] Failed to unload native library for plugin '{}' ({})", id, path);
                it->plugin  = nullptr;
                it->destroy = nullptr;
                return false;
            }
            m_Loaded.erase(eraseIt);
            VULTRA_CORE_INFO("[PluginManager] Plugin '{}' native library unloaded.", id);
            return true;
        }
        return false;
    }

    void PluginManager::uninstallAll(EngineContext& ctx)
    {
        for (auto& it : std::ranges::reverse_view(m_Loaded))
        {
            if (!it.uninstalled && it.plugin)
            {
                it.plugin->uninstall(ctx);
                it.uninstalled = true;
            }
        }
    }

    void PluginManager::unloadAll(EngineContext& ctx)
    {
        uninstallAll(ctx);

        for (auto& it : std::ranges::reverse_view(m_Loaded))
        {
            if (it.destroy && it.plugin)
                it.destroy(it.plugin);

            (void)closeLib(it.handle);
        }

        m_Loaded.clear();
    }
} // namespace vultra
