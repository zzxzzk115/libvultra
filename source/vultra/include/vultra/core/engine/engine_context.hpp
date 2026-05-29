#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/base/logger.hpp"
#include "vultra/core/rhi/structs/frame_index.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <vbase/module/module_registry.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstdint>

#if defined(__ANDROID__)
struct ANativeWindow;
struct android_app;
#endif

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
                bool        decorated {true};
                bool        visible {true};
#if defined(__ANDROID__)
                struct AndroidConfig
                {
                    android_app*   app {nullptr};
                    ANativeWindow* nativeWindow {nullptr};
                    const int*     destroyRequested {nullptr};
                } android;
#endif
            } window;

            struct RenderConfig
            {
                enum class BuiltinShaderLibrary : uint8_t
                {
                    eAuto,
                    eHighend,
                    eCompatibility,
                };

                rhi::FrameIndex::ValueType       numFramesInFlight {2};
#if defined(__EMSCRIPTEN__)
                rhi::RenderBackendApi            backendApi {rhi::RenderBackendApi::eWebGPU};
#else
                rhi::RenderBackendApi            backendApi {rhi::RenderBackendApi::eVulkan};
#endif
                rhi::RenderDeviceFeatureFlagBits renderDeviceFeatureFlag {rhi::RenderDeviceFeatureFlagBits::eNormal};
                BuiltinShaderLibrary            builtinShaderLibrary {BuiltinShaderLibrary::eAuto};
                std::string                     renderPipelineAsset {};
                std::string                     renderPipelineRendererKey;
                rhi::VerticalSync                vSyncConfig {rhi::VerticalSync::eDisabled};
                rhi::SwapchainFormat             swapchainFormat {rhi::SwapchainFormat::eLinear};
                bool                             enableValidation {rhi::defaultRenderDiagnosticsEnabled()};
                bool                             enableDebugMarkers {rhi::defaultRenderDiagnosticsEnabled()};
                bool                             enableRenderDoc {rhi::defaultRenderDiagnosticsEnabled()};

                struct XRConfig
                {
                    bool mirror {true};
                    bool autoStartSessionFromScene {true};
                    bool runtimeCameraOverride {false};
                } xr;
            } render;

            struct LogConfig
            {
                Logger::Level level {Logger::Level::eTrace};
            } log;

            struct AssetConfig
            {
                bool        loadFromVPK {false};
                bool        enableImportScan {true};
                std::string assetRoot {"resources"};
                std::string importedFolder {"imported"};
                std::string registryFile {"asset_registry.tsv"};
                std::string vpkFile {"resources.vpk"};
                bool        asyncLoading {true};
            } asset;

            struct ImGuiConfig
            {
                bool        enableMultiview {true};
                bool        enableDocking {false};
                std::string imguiIniFile {"imgui.ini"};
            } imgui;

            // Writable app-private directory used for runtime debug outputs and persisted UI state.
            // On Android this should point at internalDataPath.
            std::string writableRoot {};
        } config;

        // Per-frame state (optional)
        uint64_t frameIndex = 0;
        bool     minimized  = false;
    };
} // namespace vultra
