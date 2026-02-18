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

    using CreatePluginFn  = EnginePlugin* (*)();
    using DestroyPluginFn = void (*)(EnginePlugin*);
} // namespace vultra

// Required exported symbols in plugin shared library:
//   VULTRA_PLUGIN_API vultra::EnginePlugin* vultraCreatePlugin();
//   VULTRA_PLUGIN_API void vultraDestroyPlugin(vultra::EnginePlugin*);
