#pragma once

#include "vultra/core/base/api.hpp"

#include <vbase/module/module_registry.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class FramePipeline;
    class FeatureManager;
    class PluginManager;

    struct VULTRA_API EngineContext final
    {
        // NOTE: These registries must be unique in the process.
        // Never put them as header-level statics/singletons.
        vbase::ModuleRegistry  modules;
        vbase::ServiceRegistry services;

        // Engine framework components
        FramePipeline*  framePipeline  = nullptr; // non-owning
        FeatureManager* featureManager = nullptr; // non-owning
        PluginManager*  pluginManager  = nullptr; // non-owning

        // Per-frame state (optional)
        uint64_t frameIndex = 0;
        bool     minimized  = false;
    };
} // namespace vultra
