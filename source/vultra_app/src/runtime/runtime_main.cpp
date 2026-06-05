// Vultra standalone runtime player.
//
// This is the editor-free "player" that runs a packaged project (a .vpk). It is the single
// source for every platform's export template: it builds for desktop, wasm (Emscripten) and
// android. Unlike the editor entry in vultra_app/src/main.cpp, it pulls in NO editor / launcher
// / MCP / vasset-import code, so it compiles on platforms where those are unavailable.
//
// The logic here mirrors the Runtime-mode path of VultraStandaloneApp (see main.cpp) without
// the editor coupling.

#include "app_state.hpp"
#include "launch_options.hpp"
#include "vproject.hpp"

#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/plugin/plugin_manifest.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/world.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::string_view stripResScheme(std::string_view uri)
    {
        constexpr std::string_view kScheme = "res://";
        if (uri.starts_with(kScheme))
            uri.remove_prefix(kScheme.size());
        while (!uri.empty() && uri.front() == '/')
            uri.remove_prefix(1);
        return uri;
    }

    std::string rendererKeyFromRenderGraphUri(std::string_view uri)
    {
        auto filename = std::filesystem::path(std::string(stripResScheme(uri))).filename().generic_string();
        constexpr std::string_view suffix = ".vrg.json";
        if (filename.ends_with(suffix))
            filename.resize(filename.size() - suffix.size());
        return filename.empty() ? "custom" : filename;
    }

    std::vector<std::string> renderGraphUrisFromRegistry(const vasset::VAssetRegistry& registry)
    {
        std::vector<std::string> uris;
        for (const auto& [_, entry] : registry.getRegistry())
        {
            static_cast<void>(_);
            if (entry.type != vasset::VAssetType::eRenderGraphJson)
                continue;

            std::string logicalPath = !entry.sourcePath.empty() ? entry.sourcePath : entry.importedPath;
            if (logicalPath.empty())
                continue;
            std::replace(logicalPath.begin(), logicalPath.end(), '\\', '/');
            if (logicalPath.starts_with("res://"))
                uris.push_back(std::move(logicalPath));
            else
                uris.push_back("res://" + logicalPath);
        }

        std::sort(uris.begin(), uris.end());
        uris.erase(std::unique(uris.begin(), uris.end()), uris.end());
        return uris;
    }

    void applyProjectState(vultra_app::AppState& state, const vultra_app::LaunchOptions& options)
    {
        if (options.projectPath.empty())
            return;

        state.currentProject = options.projectPath;
        if (auto project = vultra_app::loadVProject(options.projectPath); project.has_value())
        {
            state.currentProject            = project->projectDir;
            state.currentProjectName        = project->name;
            state.currentAssetRoot          = project->assetRoot;
            state.currentDefaultScene       = project->defaultScene;
            state.currentBuildScenes        = project->buildScenes;
            state.currentEditingRenderGraph = project->editingRenderGraph;
        }
    }

    std::string defaultRuntimeSceneUri(const vultra_app::LaunchOptions& options, const vultra_app::AppState& state)
    {
        if (!options.sceneUri.empty())
            return options.sceneUri;
        if (!state.currentDefaultScene.empty())
            return state.currentDefaultScene;
        return "res://scenes/main.vscn";
    }

    class VultraRuntimeApp final : public vultra::DemoAppHost
    {
    public:
        explicit VultraRuntimeApp(vultra_app::LaunchOptions options) : m_Options(std::move(options))
        {
            m_State.mode       = vultra_app::AppMode::Runtime;
            m_State.renderMode = m_Options.renderMode;
            applyProjectState(m_State, m_Options);
            m_VpkPath = vultra_app::findDefaultVpk(m_Options);
        }

    private:
        std::string_view demoWindowTitle() const override
        {
            if (!m_State.currentProjectName.empty())
                return m_State.currentProjectName;
            return "VultraEngine";
        }

        bool demoEnableExperimentalWebGPUContent() const override { return true; }

        void onConfigureDemo(vultra::Engine& engine) override
        {
            auto& config = engine.ctx().config;

            if (!m_Options.pluginsDir.empty())
            {
                // Explicit --plugins-dir is an opt-in: discover and enable every plugin there.
                config.plugin.directory = m_Options.pluginsDir;
                for (const auto& manifest : vultra::discoverPlugins(m_Options.pluginsDir))
                    config.plugin.enabled.push_back(manifest.id);
            }

            if (config.render.backendApi == vultra::rhi::RenderBackendApi::eVulkan)
            {
                config.render.renderDeviceFeatureFlag = vultra::rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline;
            }

            const bool shouldRequestXR = m_Options.xr.value_or(true);
            if (shouldRequestXR)
            {
                if (config.render.backendApi == vultra::rhi::RenderBackendApi::eVulkan)
                {
                    config.render.renderDeviceFeatureFlag =
                        config.render.renderDeviceFeatureFlag | vultra::rhi::RenderDeviceFeatureFlagBits::eXR;
                }
                else if (m_Options.xr.value_or(false))
                {
                    VULTRA_CLIENT_WARN("[VultraRuntime] --xr requested, but XR is only supported on the Vulkan backend.");
                }
            }
            else
            {
                config.render.renderDeviceFeatureFlag =
                    config.render.renderDeviceFeatureFlag & ~vultra::rhi::RenderDeviceFeatureFlagBits::eXR;
            }
            if (m_Options.xrMirror.has_value())
                config.render.xr.mirror = *m_Options.xrMirror;

            if (m_Options.validation.has_value())
                config.render.enableValidation = *m_Options.validation;
            if (m_Options.debugMarkers.has_value())
                config.render.enableDebugMarkers = *m_Options.debugMarkers;
            if (m_Options.renderDoc.has_value())
                config.render.enableRenderDoc = *m_Options.renderDoc;

            if (m_Options.renderMode == "offscreen" || m_Options.renderMode == "none")
                config.window.visible = false;

            if (m_VpkPath.has_value())
            {
                config.asset.loadFromVPK = true;
                config.asset.assetRoot   = "/";
                config.asset.vpkFile     = m_VpkPath->generic_string();

                std::string manifestError;
                if (auto manifest = vultra_app::loadVPackageManifestFromVpk(*m_VpkPath, &manifestError);
                    manifest.has_value())
                {
                    if (!manifest->name.empty())
                        config.window.title = manifest->name;
                    if (m_Options.sceneUri.empty())
                        m_Options.sceneUri = manifest->entryScene;
                    // Load plugins bundled into the package from the mounted res:// VFS.
                    if (!manifest->pluginDirs.empty())
                    {
                        config.plugin.loadFromVPK = true;
                        config.plugin.packaged    = manifest->pluginDirs;
                    }
                }
                else
                {
                    VULTRA_CLIENT_WARN("[VultraRuntime] VPK package manifest unavailable: {}", manifestError);
                }
                return;
            }

            VULTRA_CLIENT_ERROR(
                "[VultraRuntime] No VPK found. Pass --vpk <package.vpk> or place a resources.vpk next to the runtime.");
        }

        void onPostConfigureDemo(vultra::Engine& engine) override
        {
            auto* renderService = engine.ctx().services.tryGet<vultra::IRenderService>();
            auto* assetService  = engine.ctx().services.tryGet<vultra::IAssetService>();
            if (renderService)
            {
                VULTRA_CLIENT_INFO("[VultraRuntime] Loading render graphs from asset registry");
                auto renderGraphUris =
                    assetService ? renderGraphUrisFromRegistry(assetService->registry()) : std::vector<std::string> {};
                if (!m_State.currentEditingRenderGraph.empty() &&
                    std::find(renderGraphUris.begin(), renderGraphUris.end(), m_State.currentEditingRenderGraph) ==
                        renderGraphUris.end())
                {
                    renderGraphUris.push_back(m_State.currentEditingRenderGraph);
                }
                for (const auto& uri : renderGraphUris)
                {
                    const auto rendererKey = rendererKeyFromRenderGraphUri(uri);
                    renderService->reloadRenderPipeline(uri, rendererKey);
                    if (m_RuntimeRendererKey.empty() || uri == "res://render/default.vrg.json")
                        m_RuntimeRendererKey = rendererKey;
                }
                VULTRA_CLIENT_INFO("[VultraRuntime] Loaded {} render graph(s)", renderGraphUris.size());
            }

            if (auto* cameraService = engine.ctx().services.tryGet<vultra::ICameraService>())
            {
                cameraService->clearManualCameras();
                cameraService->setWorldCamerasEnabled(true);
                cameraService->setWorldXRCamerasEnabled(true);
            }

            m_RuntimeSceneUri  = defaultRuntimeSceneUri(m_Options, m_State);
            m_RuntimeSceneLoad = engine.ctx().services.require<vultra::ISceneService>().loadSceneAsync(m_RuntimeSceneUri);
            const auto vpk     = m_VpkPath ? m_VpkPath->generic_string() : std::string {};
            VULTRA_CLIENT_INFO("[VultraRuntime] Loading scene '{}' from VPK '{}'", m_RuntimeSceneUri, vpk);
        }

        void onBeforeEngineTick(vultra::fsec /*dt*/) override { updateRuntimeSceneLoad(); }

        void updateRuntimeSceneLoad()
        {
            if (!m_RuntimeSceneLoad || m_RuntimeSceneLoaded)
                return;

            auto* sceneService = engineCtx().services.tryGet<vultra::ISceneService>();
            auto* worldService = engineCtx().services.tryGet<vultra::IWorldService>();
            if (!sceneService || !worldService)
                return;

            const auto status = sceneService->sceneLoadStatus(m_RuntimeSceneLoad);
            if (status.state == vultra::SceneLoadState::eLoading)
                return;

            if (status.state == vultra::SceneLoadState::eFailed || status.state == vultra::SceneLoadState::eInvalid)
            {
                VULTRA_CLIENT_ERROR("[VultraRuntime] Failed to load scene '{}': {}", m_RuntimeSceneUri, status.message);
                sceneService->releaseSceneLoad(m_RuntimeSceneLoad);
                m_RuntimeSceneLoad = {};
                return;
            }

            const auto root =
                sceneService->instantiateLoadedScene(m_RuntimeSceneLoad, worldService->world(), entt::null, false);
            sceneService->releaseSceneLoad(m_RuntimeSceneLoad);
            m_RuntimeSceneLoad = {};
            if (root == entt::null)
            {
                VULTRA_CLIENT_ERROR("[VultraRuntime] Failed to instantiate scene '{}'", m_RuntimeSceneUri);
                return;
            }

            bool  hasSceneCamera = false;
            auto& reg            = worldService->world().registry();
            auto  view           = reg.view<vultra::CameraComponent>();
            for (auto entity : view)
            {
                hasSceneCamera = true;
                auto& camera   = view.get<vultra::CameraComponent>(entity);
                if (!m_RuntimeRendererKey.empty() && (camera.rendererKey.empty() || camera.rendererKey == "universal"))
                    camera.rendererKey = m_RuntimeRendererKey;
            }
            if (hasSceneCamera)
            {
                if (auto* cameraService = engineCtx().services.tryGet<vultra::ICameraService>())
                {
                    cameraService->clearManualCameras();
                    cameraService->setWorldCamerasEnabled(true);
                    cameraService->setWorldXRCamerasEnabled(true);
                }
            }

            m_RuntimeSceneLoaded = true;
            const auto vpk       = m_VpkPath ? m_VpkPath->generic_string() : std::string {};
            VULTRA_CLIENT_INFO("[VultraRuntime] Loaded scene '{}' from VPK '{}'", m_RuntimeSceneUri, vpk);
        }

        void onBeforeShutdown(vultra::Engine& engine) override
        {
            if (m_RuntimeSceneLoad)
            {
                if (auto* sceneService = engine.ctx().services.tryGet<vultra::ISceneService>())
                    sceneService->releaseSceneLoad(m_RuntimeSceneLoad);
                m_RuntimeSceneLoad = {};
            }
        }

        vultra_app::LaunchOptions            m_Options;
        std::optional<std::filesystem::path> m_VpkPath;
        vultra::SceneLoadHandle              m_RuntimeSceneLoad;
        std::string                          m_RuntimeSceneUri;
        std::string                          m_RuntimeRendererKey;
        bool                                 m_RuntimeSceneLoaded {false};
        mutable vultra_app::AppState         m_State;
    };
} // namespace

#if defined(__ANDROID__)
#include <vultra/platform/android/android_app_runtime_context.hpp>
#include <android/log.h>

extern "C" void vultra_android_run(const vultra::platform::android::AndroidAppRuntimeContext* runtimeContext)
try
{
    // Android delivers assets via the APK; the launcher extracts resources.vpk to the app files
    // dir and the default VPK scan picks it up. (Android export wiring is planned; see plan.)
    vultra_app::LaunchOptions options;
    VultraRuntimeApp          app {std::move(options)};
    if (runtimeContext != nullptr)
        app.setAndroidRuntimeContext(*runtimeContext);
    (void)app.run();
}
catch (const std::exception& e)
{
    __android_log_print(ANDROID_LOG_ERROR, "VULTRA_CORE", "[VultraRuntime] Unhandled exception: %s", e.what());
}
catch (...)
{
    __android_log_print(ANDROID_LOG_ERROR, "VULTRA_CORE", "[VultraRuntime] Unknown unhandled exception");
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> args;
    if (argc > 1 && argv != nullptr)
    {
        args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] != nullptr)
                args.emplace_back(argv[i]);
        }
    }

    auto options = vultra_app::parseLaunchOptions(args);
    if (options.showHelp)
    {
        vultra_app::printUsage();
        return 0;
    }

    VultraRuntimeApp app {std::move(options)};
    return app.run(argc, argv);
}
#endif
