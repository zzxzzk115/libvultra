#pragma once

#include <vultra/core/base/api.hpp>

namespace vultra
{
    struct EngineContext;

    // Native plugin ABI version. Bump on any breaking change to EnginePlugin
    // or the exported-symbol contract below. PluginManager refuses to load a
    // native plugin whose vultraPluginAbiVersion() does not match, with a
    // clear "rebuild your plugin" error instead of a crash.
    inline constexpr unsigned int kEnginePluginAbiVersion = 2;

    class VULTRA_API EnginePlugin
    {
    public:
        virtual ~EnginePlugin() = default;

        virtual const char* name() const = 0;

        virtual bool install(EngineContext& ctx)   = 0;
        virtual void uninstall(EngineContext& ctx) = 0;

        // Per-frame tick (ABI v2). Default no-op so a plugin only overrides it
        // when it needs frame updates. Called by PluginSystem each frame after
        // all plugins are installed; `dt` is the unscaled frame delta seconds.
        virtual void update(EngineContext& /*ctx*/, float /*dt*/) {}
    };

    using CreatePluginFn  = EnginePlugin* (*)();
    using DestroyPluginFn = void (*)(EnginePlugin*);
    using SetPluginRootFn = void (*)(const char* absolutePath);
    using AbiVersionFn    = unsigned int (*)();
} // namespace vultra

// Required exported symbols in plugin shared library:
//   VULTRA_PLUGIN_API vultra::EnginePlugin* vultraCreatePlugin();
//   VULTRA_PLUGIN_API void vultraDestroyPlugin(vultra::EnginePlugin*);
//
// Optional exported symbols:
//   VULTRA_PLUGIN_API unsigned int vultraPluginAbiVersion();
//     Returns vultra::kEnginePluginAbiVersion the plugin was built against.
//     When absent, the plugin is assumed to predate ABI versioning (v1) and is
//     loaded only if the engine's current ABI is v1; otherwise it is rejected.
//   VULTRA_PLUGIN_API void vultraSetPluginRoot(const char* absolutePath);
//     Called once per load, after vultraCreatePlugin() and before install(), with the absolute
//     path of the plugin's own root directory (UTF-8). Plugins that bundle runtime payloads
//     (DLLs, data files) next to their manifest use this to locate them without any global state.
