#pragma once

#include <vultra/core/base/api.hpp>

namespace vultra
{
    struct EngineContext;

    class VULTRA_API EnginePlugin
    {
    public:
        virtual ~EnginePlugin() = default;

        virtual const char* name() const = 0;

        virtual bool install(EngineContext& ctx)   = 0;
        virtual void uninstall(EngineContext& ctx) = 0;
    };

    using CreatePluginFn        = EnginePlugin* (*)();
    using DestroyPluginFn       = void (*)(EnginePlugin*);
    using SetPluginRootFn       = void (*)(const char* absolutePath);
} // namespace vultra

// Required exported symbols in plugin shared library:
//   VULTRA_PLUGIN_API vultra::EnginePlugin* vultraCreatePlugin();
//   VULTRA_PLUGIN_API void vultraDestroyPlugin(vultra::EnginePlugin*);
//
// Optional exported symbols:
//   VULTRA_PLUGIN_API void vultraSetPluginRoot(const char* absolutePath);
//     Called once per load, after vultraCreatePlugin() and before install(), with the absolute
//     path of the plugin's own root directory (UTF-8). Plugins that bundle runtime payloads
//     (DLLs, data files) next to their manifest use this to locate them without any global state.
