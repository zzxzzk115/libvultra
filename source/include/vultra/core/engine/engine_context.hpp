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
            struct WindowConfig
            {
                std::string title {"Vultra App"};
                uint32_t    width {1024};
                uint32_t    height {768};
                bool        resizable {true};
                bool        fullscreen {false};
            } window;

            struct RenderConfig
            {
                rhi::FrameIndex::ValueType       numFramesInFlight {2};
                rhi::RenderDeviceFeatureFlagBits renderDeviceFeatureFlag {rhi::RenderDeviceFeatureFlagBits::eNormal};
                rhi::VerticalSync                vSyncConfig {rhi::VerticalSync::eDisabled};
                rhi::Swapchain::Format           swapchainFormat {rhi::Swapchain::Format::eLinear};

                struct XRConfig
                {
                    bool mirror {true};
                } xr;
            } render;

            struct LogConfig
            {
                Logger::Level level {Logger::Level::eTrace};
            } log;

            struct AssetConfig
            {
                bool loadFromVPK {false};
            } asset;

            struct ImGuiConfig
            {
                bool        enableMultiview {true};
                bool        enableDocking {false};
                std::string imguiIniFile {"imgui.ini"};
            } imgui;
        } config;

        // Per-frame state (optional)
        uint64_t frameIndex = 0;
        bool     minimized  = false;
    };
} // namespace vultra
