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

        bool load(const std::string& path, EngineContext& ctx);
        void unloadAll(EngineContext& ctx);

    private:
        struct Loaded
        {
            void*           handle  = nullptr;
            EnginePlugin*   plugin  = nullptr;
            DestroyPluginFn destroy = nullptr;
            std::string     path;
        };

        std::vector<Loaded> m_Loaded;
    };
} // namespace vultra
