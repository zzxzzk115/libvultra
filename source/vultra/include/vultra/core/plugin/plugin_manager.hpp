#pragma once

#include <vultra/core/base/api.hpp>
#include <vultra/core/plugin/engine_plugin.hpp>

#include <string>
#include <vector>

namespace vultra
{
    struct EngineContext;

    class VULTRA_API PluginManager final
    {
    public:
        PluginManager()                                = default;
        PluginManager(const PluginManager&)            = delete;
        PluginManager& operator=(const PluginManager&) = delete;
        PluginManager(PluginManager&&)                 = delete;
        PluginManager& operator=(PluginManager&&)      = delete;
        ~PluginManager()                               = default;

        bool load(const std::string& id, const std::string& path, EngineContext& ctx);
        bool unload(const std::string& id, EngineContext& ctx);
        bool isLoaded(const std::string& id) const;
        void uninstallAll(EngineContext& ctx);
        void unloadAll(EngineContext& ctx);

        // Per-frame tick for every loaded native plugin (ABI v2). Called by
        // PluginSystem in load order.
        void update(EngineContext& ctx, float dt);

    private:
        struct Loaded
        {
            void*           handle  = nullptr;
            EnginePlugin*   plugin  = nullptr;
            DestroyPluginFn destroy = nullptr;
            std::string     id;
            std::string     path;
            bool            uninstalled = false;
        };

        std::vector<Loaded> m_Loaded;
    };
} // namespace vultra
