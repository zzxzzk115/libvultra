#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/base/logger.hpp"
#include "vultra/core/rhi/frame_index.hpp"
#include "vultra/core/rhi/render_device.hpp"

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
        FramePipeline*  framePipeline {nullptr};  // non-owning
        FeatureManager* featureManager {nullptr}; // non-owning
        PluginManager*  pluginManager {nullptr};  // non-owning

        struct Config
        {
            std::string title {"Vultra App"};
            uint32_t    windowWidth {1024};
            uint32_t    windowHeight {768};

            rhi::FrameIndex::ValueType       numFramesInFlight {2};
            rhi::RenderDeviceFeatureFlagBits renderDeviceFeatureFlag {rhi::RenderDeviceFeatureFlagBits::eNormal};
            Logger::Level                    logLevel {Logger::Level::eTrace};
            rhi::VerticalSync                vSyncConfig {rhi::VerticalSync::eAdaptive};
            rhi::Swapchain::Format           swapchainFormat {rhi::Swapchain::Format::eLinear};
        } config;

        // Per-frame state (optional)
        uint64_t frameIndex = 0;
        bool     minimized  = false;
    };
} // namespace vultra
