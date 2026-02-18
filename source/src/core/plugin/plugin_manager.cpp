#include "vultra/core/plugin/plugin_manager.hpp"
#include "vultra/core/engine/engine_context.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>

#include <ranges>
#endif

namespace vultra
{
    static void* openLib(const char* path)
    {
#if defined(_WIN32)
        return (void*)::LoadLibraryA(path);
#else
        return ::dlopen(path, RTLD_NOW);
#endif
    }

    static void closeLib(void* h)
    {
#if defined(_WIN32)
        if (h)
            ::FreeLibrary((HMODULE)h);
#else
        if (h)
            ::dlclose(h);
#endif
    }

    static void* getSym(void* h, const char* sym)
    {
#if defined(_WIN32)
        return (void*)::GetProcAddress((HMODULE)h, sym);
#else
        return ::dlsym(h, sym);
#endif
    }

    bool PluginManager::load(const std::string& path, EngineContext& ctx)
    {
        void* h = openLib(path.c_str());
        if (!h)
            return false;

        auto create  = reinterpret_cast<CreatePluginFn>(getSym(h, "vultraCreatePlugin"));
        auto destroy = reinterpret_cast<DestroyPluginFn>(getSym(h, "vultraDestroyPlugin"));

        if (!create || !destroy)
        {
            closeLib(h);
            return false;
        }

        EnginePlugin* p = create();
        if (!p)
        {
            closeLib(h);
            return false;
        }

        if (!p->install(ctx))
        {
            destroy(p);
            closeLib(h);
            return false;
        }

        m_Loaded.push_back(Loaded {.handle = h, .plugin = p, .destroy = destroy, .path = path});
        return true;
    }

    void PluginManager::unloadAll(EngineContext& ctx)
    {
        for (auto& it : std::ranges::reverse_view(m_Loaded))
        {
            if (it.plugin)
                it.plugin->uninstall(ctx);

            if (it.destroy && it.plugin)
                it.destroy(it.plugin);

            closeLib(it.handle);
        }

        m_Loaded.clear();
    }
} // namespace vultra
