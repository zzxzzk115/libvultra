#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"

#include "editor_app/editor_app.hpp"
#include "vproject.hpp"

#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/world.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <vasset/vasset_type.hpp>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
        std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        nlohmann::json toolJson(nlohmann::json payload)
        {
            return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", payload.dump(2)}}})}};
        }

        nlohmann::json toolError(std::string message)
        {
            return {{"isError", true},
                    {"content",
                     nlohmann::json::array({{{"type", "text"},
                                              {"text", nlohmann::json({{"ok", false}, {"error", std::move(message)}}).dump(2)}}})}};
        }
        const char* appModeName(const AppMode mode)
        {
            switch (mode)
            {
                case AppMode::Runtime: return "runtime";
                case AppMode::Launcher: return "launcher";
                case AppMode::Editor: return "editor";
            }
            return "unknown";
        }

        const char* backendName(const vultra::rhi::RenderBackendApi api)
        {
            switch (api)
            {
                case vultra::rhi::RenderBackendApi::eAuto: return "auto";
                case vultra::rhi::RenderBackendApi::eVulkan: return "vulkan";
                case vultra::rhi::RenderBackendApi::eWebGPU: return "webgpu";
            }
            return "unknown";
        }

        std::string stripScheme(std::string_view uri)
        {
            constexpr std::string_view kResScheme     = "res://";
            constexpr std::string_view kBuiltinScheme = "builtin://";
            if (uri.starts_with(kResScheme))
                uri.remove_prefix(kResScheme.size());
            else if (uri.starts_with(kBuiltinScheme))
                uri.remove_prefix(kBuiltinScheme.size());
            while (!uri.empty() && (uri.front() == '/' || uri.front() == '\\'))
                uri.remove_prefix(1);
            return std::string(uri);
        }

        std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto filename = std::filesystem::path(stripScheme(uri)).filename().generic_string();
            constexpr std::string_view suffix = ".vrg.json";
            if (filename.ends_with(suffix))
                filename.resize(filename.size() - suffix.size());
            if (filename.empty())
                filename = "custom";
            for (auto& ch : filename)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return filename;
        }

        std::string renderGraphUriForRendererKey(std::string rendererKey, EditorContext& ctx)
        {
            if (rendererKey.empty())
                rendererKey = rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph);

            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (assetService)
            {
                std::vector<std::string> uris;
                for (const auto& [_, entry] : assetService->registry().getRegistry())
                {
                    static_cast<void>(_);
                    if (entry.type != vasset::VAssetType::eRenderGraphJson)
                        continue;

                    std::string logicalPath = !entry.sourcePath.empty() ? entry.sourcePath : entry.importedPath;
                    if (logicalPath.empty())
                        continue;
                    std::replace(logicalPath.begin(), logicalPath.end(), '\\', '/');
                    if (!logicalPath.starts_with("res://"))
                        logicalPath = "res://" + logicalPath;
                    uris.push_back(std::move(logicalPath));
                }
                if (!ctx.state.currentEditingRenderGraph.empty())
                    uris.push_back(ctx.state.currentEditingRenderGraph);

                for (const auto& uri : uris)
                {
                    if (rendererKeyFromRenderGraphUri(uri) == rendererKey)
                        return uri;
                }
            }

            if (rendererKey == "universal" || rendererKey == "universal_rt" || rendererKey == "universal_compat")
                return "builtin://render/" + rendererKey + ".vrg.json";
            if (rendererKey == "editor-ui2d")
                return "builtin://render/ui_editor.vrg.json";
            if (!ctx.state.currentEditingRenderGraph.empty() &&
                rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph) == rendererKey)
                return ctx.state.currentEditingRenderGraph;
            return {};
        }

        nlohmann::json vec3Json(const glm::vec3& v)
        {
            return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
        }

        nlohmann::json quatJson(const glm::quat& q)
        {
            return {{"w", q.w}, {"x", q.x}, {"y", q.y}, {"z", q.z}};
        }

        nlohmann::json vec4Json(const glm::vec4& v)
        {
            return {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
        }

        nlohmann::json cameraJson(vultra::World& world, const entt::entity entity, EditorContext& ctx)
        {
            auto& camera = world.registry().get<vultra::CameraComponent>(entity);
            nlohmann::json out = {
                {"entity", static_cast<uint32_t>(entity)},
                {"primary", camera.primary},
                {"projection", camera.projection == 1 ? "orthographic" : "perspective"},
                {"fovY", camera.fovY},
                {"orthographicHeight", camera.orthographicHeight},
                {"zNear", camera.zNear},
                {"zFar", camera.zFar},
                {"clearMode", camera.clearMode == 1 ? "environment" : "solid_color"},
                {"clearColor", vec4Json(camera.clearColor)},
                {"priority", camera.priority},
                {"rendererKey", camera.rendererKey},
                {"renderGraph", renderGraphUriForRendererKey(camera.rendererKey, ctx)},
            };
            if (const auto* id = world.registry().try_get<vultra::IDComponent>(entity))
                out["uuid"] = id->uuid.toString();
            if (const auto* name = world.registry().try_get<vultra::NameComponent>(entity))
                out["name"] = name->name;
            if (const auto* transform = world.registry().try_get<vultra::TransformComponent>(entity))
            {
                out["transform"] = {
                    {"position", vec3Json(transform->position)},
                    {"rotation", quatJson(transform->rotation)},
                    {"scale", vec3Json(transform->scale)},
                };
            }
            return out;
        }

        entt::entity findMainCamera(vultra::World& world)
        {
            auto view = world.registry().view<vultra::CameraComponent>();
            entt::entity best = entt::null;
            int bestPriority = std::numeric_limits<int>::min();
            for (auto entity : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(entity);
                if (camera.primary)
                    return entity;
                if (best == entt::null || camera.priority > bestPriority)
                {
                    best = entity;
                    bestPriority = camera.priority;
                }
            }
            return best;
        }

        glm::mat4 localTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 worldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto  local     = localTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;
            return worldTransformMatrix(reg, hierarchy->parent) * local;
        }

        glm::mat4 projectionMatrix(const vultra::CameraComponent& camera, const float aspect)
        {
            const float zNear = std::max(camera.zNear, 0.0001f);
            const float zFar  = std::max(camera.zFar, zNear + 0.0001f);
            if (camera.projection == 1u)
            {
                const float height = std::max(camera.orthographicHeight, 0.0001f);
                const float width  = height * std::max(aspect, 0.0001f);
                return glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, zNear, zFar);
            }
            return glm::perspectiveRH_ZO(glm::radians(camera.fovY), std::max(aspect, 0.0001f), zNear, zFar);
        }

        std::optional<vultra::RenderCamera> makeMcpFrameGraphCamera(vultra::World&             world,
                                                                     const std::string_view     rendererOverride,
                                                                     const vultra::rhi::Extent2D extent)
        {
            const auto entity = findMainCamera(world);
            if (entity == entt::null)
                return std::nullopt;

            auto&       reg    = world.registry();
            const auto& camera = reg.get<vultra::CameraComponent>(entity);
            const auto& id     = reg.get<vultra::IDComponent>(entity);
            const float aspect = static_cast<float>(std::max(extent.width, 1u)) /
                                 static_cast<float>(std::max(extent.height, 1u));

            vultra::RenderCamera out {};
            out.uuid                    = id.uuid;
            out.name                    = reg.all_of<vultra::NameComponent>(entity) ?
                                              reg.get<vultra::NameComponent>(entity).name :
                                              "Camera";
            out.priority                = camera.priority;
            out.view                    = glm::inverse(worldTransformMatrix(reg, entity));
            out.projection              = projectionMatrix(camera, aspect);
            out.zNear                   = std::max(camera.zNear, 0.0001f);
            out.zFar                    = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY                    = glm::radians(camera.fovY);
            out.clearValue              = camera.clearColor;
            out.clearMode               = camera.clearMode;
            out.renderImGui             = false;
            out.debugEntityIdOutput     = false;
            out.selectionOutlineEnabled = false;
            out.allowUpscaler           = false;
            out.overrideFrameTime       = true;
            out.frameTimeSeconds        = 0.0f;
            out.frameDeltaSeconds       = 0.0f;
            out.rendererKey             = !rendererOverride.empty() ? std::string(rendererOverride) :
                                          camera.rendererKey.empty() ? "universal" :
                                                                       camera.rendererKey;
            return out;
        }

        std::filesystem::path projectAssetRoot(const EditorContext& ctx)
        {
            if (ctx.state.currentProject.empty())
                return {};
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string safeFileStem(std::string text)
        {
            if (text.empty())
                text = "texture";
            for (auto& ch : text)
            {
                const auto uch = static_cast<unsigned char>(ch);
                if (!std::isalnum(uch) && ch != '-' && ch != '_')
                    ch = '_';
            }
            while (text.find("__") != std::string::npos)
                text.replace(text.find("__"), 2, "_");
            if (text.size() > 120)
                text.resize(120);
            return text;
        }

        bool textureMatchesFilter(const vultra::FrameGraphDebugTexture& texture,
                                  const std::string&                    filter,
                                  const std::string&                    camera,
                                  const std::string&                    renderer)
        {
            if (!camera.empty() && texture.camera != camera)
                return false;
            if (!renderer.empty() && texture.renderer != renderer)
                return false;
            if (filter.empty())
                return true;
            const auto needle = lowerAscii(filter);
            const auto haystack = lowerAscii(texture.camera + " " + texture.renderer + " " + texture.name + " " +
                                             texture.key + " " + texture.resourceKey + " " +
                                             texture.transientResourceKey);
            return haystack.find(needle) != std::string::npos;
        }

        nlohmann::json serializeScope(const vultra::RuntimeProfiler::ScopeNode& scope)
        {
            return {
                {"name", scope.name},
                {"parent", scope.parent},
                {"depth", scope.depth},
                {"calls", scope.callCount},
                {"totalMs", scope.totalMs},
                {"selfMs", scope.selfMs},
                {"gpuTotalMs", scope.gpuTotalMs},
                {"gpuSelfMs", scope.gpuSelfMs},
            };
        }

        nlohmann::json serializeProfilerFrame(const vultra::RuntimeProfiler::FrameStats& frame)
        {
            nlohmann::json cpuScopes = nlohmann::json::array();
            for (const auto& scope : frame.cpuScopeTree)
                cpuScopes.push_back(serializeScope(scope));

            nlohmann::json gpuScopes = nlohmann::json::array();
            for (const auto& scope : frame.gpuScopeTree)
                gpuScopes.push_back(serializeScope(scope));

            return {
                {"frameIndex", frame.frameIndex},
                {"cpuFrameMs", frame.cpuFrameMs},
                {"cpuRenderMs", frame.cpuRenderMs},
                {"gpuFrameMs", frame.gpuFrameMs},
                {"drawCalls", frame.drawCalls},
                {"dispatchCalls", frame.dispatchCalls},
                {"traceRaysCalls", frame.traceRaysCalls},
                {"copyOps", frame.copyOps},
                {"updateOps", frame.updateOps},
                {"vsyncEnabled", frame.vsyncEnabled},
                {"assetCpuCacheBytes", frame.assetCpuCacheBytes},
                {"renderCpuCacheBytes", frame.renderCpuCacheBytes},
                {"gpuDeviceLocalBytes", frame.gpuDeviceLocalBytes},
                {"gpuHostVisibleBytes", frame.gpuHostVisibleBytes},
                {"gpuScopeBeginCount", frame.gpuScopeBeginCount},
                {"gpuScopeTokenCount", frame.gpuScopeTokenCount},
                {"gpuScopeResolvedCount", frame.gpuScopeResolvedCount},
                {"cpuScopes", std::move(cpuScopes)},
                {"gpuScopes", std::move(gpuScopes)},
            };
        }





    } // namespace

    nlohmann::json RuntimeMcpServer::handleRuntimeTool(std::string_view name,
                                                      const nlohmann::json& args,
                                                      EditorContext& ctx,
                                                      PendingCall* call)
    {
        if (name == "vultra.runtime.status")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto* profiler = renderService ? renderService->runtimeProfiler() : nullptr;
            const auto* profilerFrame = profiler ? profiler->selectedFrame() : nullptr;
            if (!profilerFrame && profiler && !profiler->history().empty())
                profilerFrame = &profiler->history().back();
            nlohmann::json rendererKeys = nlohmann::json::array();
            if (renderService)
            {
                for (const auto& key : renderService->rendererKeys())
                    rendererKeys.push_back(key);
            }
            return toolJson({
                {"ok", true},
                {"mode", appModeName(ctx.state.mode)},
                {"project", ctx.state.currentProject.generic_string()},
                {"projectName", ctx.state.currentProjectName},
                {"defaultScene", ctx.state.currentDefaultScene},
                {"editingRenderGraph", ctx.state.currentEditingRenderGraph},
                {"renderMode", ctx.state.renderMode},
                {"playing", ctx.state.editorPlaying},
                {"paused", ctx.state.editorPaused},
                {"stepRequested", ctx.state.editorStepRequested},
                {"frameIndex", profilerFrame ? profilerFrame->frameIndex : 0},
                {"backend", backendService ? backendName(backendService->renderDevice().getBackendApi()) : "unavailable"},
                {"xrEnabled", backendService && backendService->isXREnabled()},
                {"rendererKeys", std::move(rendererKeys)},
            });
        }

        if (name == "vultra.project.info")
        {
            const auto assetRoot = projectAssetRoot(ctx);
            return toolJson({{"ok", true},
                             {"mode", appModeName(ctx.state.mode)},
                             {"project", ctx.state.currentProject.generic_string()},
                             {"projectName", ctx.state.currentProjectName},
                             {"assetRoot", ctx.state.currentAssetRoot},
                             {"assetRootPath", assetRoot.generic_string()},
                             {"defaultScene", ctx.state.currentDefaultScene},
                             {"editingRenderGraph", ctx.state.currentEditingRenderGraph},
                             {"sceneDirty", ctx.state.sceneDirty},
                             {"projectGeneration", ctx.state.projectGeneration},
                             {"assetFileGeneration", ctx.state.assetFileGeneration},
                             {"sceneContentGeneration", ctx.state.sceneContentGeneration},
                             {"vprojectFile",
                              ctx.state.currentProject.empty() ?
                                  std::string {} :
                                  vprojectFileFor(ctx.state.currentProject, ctx.state.currentProjectName).generic_string()}});
        }

        if (name == "vultra.runtime.playback")
        {
            if (!args.is_object() || !args.contains("action") || !args["action"].is_string())
                return toolError("playback requires action");
            if (!ctx.editor)
                return toolError("editor command executor is unavailable");
            auto result = ctx.editor->executeCommand(ctx, "runtime.playback", args);
            if (!result.value("ok", false))
                return toolError(result.value("error", "runtime.playback failed"));
            return toolJson(std::move(result));
        }

        if (name == "vultra.runtime.profiler")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            auto* profiler      = renderService ? renderService->runtimeProfiler() : nullptr;
            if (!profiler)
                return toolError("render profiler is unavailable");
            profiler->setEnabled(true);
            const auto* frame = profiler->selectedFrame();
            if (!frame && !profiler->history().empty())
                frame = &profiler->history().back();
            if (!frame)
                return toolError("profiler has no captured frames");
            return toolJson({{"ok", true}, {"enabled", profiler->isEnabled()}, {"historySize", profiler->historySize()}, {"frame", serializeProfilerFrame(*frame)}});
        }

        if (name == "vultra.runtime.framegraph_snapshot")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            if (!renderService)
                return toolError("render service is unavailable");
            renderService->setFrameGraphSnapshotCaptureEnabled(true);
            const auto snapshot = std::string(renderService->lastFrameGraphSnapshot());
            return toolJson({{"ok", true}, {"captureEnabled", true}, {"snapshot", snapshot}});
        }

        if (name == "vultra.runtime.frame_resources")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto* cameraService = ctx.services ? ctx.services->tryGet<vultra::ICameraService>() : nullptr;
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!renderService)
                return toolError("render service is unavailable");
            if (!backendService)
                return toolError("render backend service is unavailable");
            if (!cameraService)
                return toolError("camera service is unavailable");
            if (!worldService)
                return toolError("world service is unavailable");

            const auto filter   = args.value("filter", std::string {});
            const auto camera   = args.value("camera", std::string {});
            const auto renderer = args.value("renderer", std::string {});
            if (call && !call->frameCaptureRequested)
            {
                auto captureCamera =
                    makeMcpFrameGraphCamera(worldService->world(), renderer, backendService->backbuffer().getExtent());
                if (!captureCamera)
                    return toolError("no scene camera is available for MCP frame resource capture");
                if (!camera.empty())
                    captureCamera->name = camera;
                call->frameCaptureCameraName = captureCamera->name;
                cameraService->removeManualCamerasByName(call->frameCaptureCameraName);
                cameraService->addManualCamera(*captureCamera);
                call->frameCaptureRequested = true;
                call->frameCaptureStartedAt = std::chrono::steady_clock::now();
                renderService->requestFrameGraphTextureDumpCapture(4, 0, filter, camera, renderer);
                call->defer = true;
                return {};
            }

            int pendingMatches = 0;
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (textureMatchesFilter(texture, filter, camera, renderer))
                    ++pendingMatches;
            }
            if (call && pendingMatches == 0)
            {
                const auto elapsed = std::chrono::steady_clock::now() - call->frameCaptureStartedAt;
                if (elapsed < std::chrono::seconds(4))
                {
                    call->defer = true;
                    return {};
                }
            }

            nlohmann::json textures = nlohmann::json::array();
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (!textureMatchesFilter(texture, filter, camera, renderer))
                    continue;
                textures.push_back({
                    {"camera", texture.camera},
                    {"renderer", texture.renderer},
                    {"name", texture.name},
                    {"key", texture.key},
                    {"resourceKey", texture.resourceKey},
                    {"transientResourceKey", texture.transientResourceKey},
                    {"layer", texture.layer},
                    {"layerCount", texture.layerCount},
                    {"imported", texture.imported},
                    {"capturable", texture.capturable},
                    {"extent", {{"width", texture.extent.width}, {"height", texture.extent.height}}},
                    {"sourceExtent", {{"width", texture.sourceExtent.width}, {"height", texture.sourceExtent.height}}},
                    {"format", std::string(vultra::rhi::toString(texture.format))},
                    {"zNear", texture.zNear},
                    {"zFar", texture.zFar},
                });
            }
            if (call && !call->frameCaptureCameraName.empty())
                cameraService->removeManualCamerasByName(call->frameCaptureCameraName);
            return toolJson({{"ok", true}, {"captureEnabled", true}, {"textures", std::move(textures)}});
        }

        if (name == "vultra.runtime.scene_context")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return toolError("world service is unavailable");

            auto& world = worldService->world();
            const auto mainCamera = findMainCamera(world);
            nlohmann::json cameras = nlohmann::json::array();
            for (auto view = world.registry().view<vultra::CameraComponent>(); auto entity : view)
                cameras.push_back(cameraJson(world, entity, ctx));

            nlohmann::json result = {
                {"ok", true},
                {"scene", {{"uri", ctx.state.currentDefaultScene}, {"dirty", ctx.state.sceneDirty}}},
                {"mainCamera", mainCamera != entt::null ? cameraJson(world, mainCamera, ctx) : nlohmann::json(nullptr)},
                {"cameras", std::move(cameras)},
            };

            if (mainCamera != entt::null && args.value("includeRenderGraphSource", false))
            {
                const auto& camera = world.registry().get<vultra::CameraComponent>(mainCamera);
                const auto uri = renderGraphUriForRendererKey(camera.rendererKey, ctx);
                auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
                nlohmann::json graph = {{"uri", uri}, {"rendererKey", camera.rendererKey}};
                if (assetService && !uri.empty())
                {
                    auto text = assetService->loadTextAssetSync(uri);
                    if (text)
                        graph["source"] = text.value();
                    else
                        graph["error"] = text.error();
                }
                else
                {
                    graph["error"] = assetService ? "render graph uri is unknown" : "asset service is unavailable";
                }
                result["mainCameraRenderGraph"] = std::move(graph);
            }
            return toolJson(std::move(result));
        }

        if (name == "vultra.runtime.rendergraph_source")
        {
            const auto rendererKey = args.value("rendererKey", std::string {});
            auto       uri         = args.value("uri", std::string {});
            if (uri.empty())
                uri = renderGraphUriForRendererKey(rendererKey, ctx);
            if (uri.empty())
                return toolError("render graph uri is unknown for renderer key: " + rendererKey);

            nlohmann::json result = {
                {"ok", true},
                {"uri", uri},
                {"rendererKey", rendererKey.empty() ? rendererKeyFromRenderGraphUri(uri) : rendererKey},
            };
            if (args.value("includeSource", true))
            {
                auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
                if (!assetService)
                    return toolError("asset service is unavailable");
                auto text = assetService->loadTextAssetSync(uri);
                if (!text)
                    return toolError("failed to load render graph '" + uri + "': " + text.error());
                result["source"] = text.value();
            }
            return toolJson(std::move(result));
        }

        if (name == "vultra.runtime.dump_frame_textures")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto* cameraService = ctx.services ? ctx.services->tryGet<vultra::ICameraService>() : nullptr;
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!renderService)
                return toolError("render service is unavailable");
            if (!backendService)
                return toolError("render backend service is unavailable");
            if (!cameraService)
                return toolError("camera service is unavailable");
            if (!worldService)
                return toolError("world service is unavailable");

            const auto outputDirectory = args.value("outputDirectory", std::string {".vultra/mcp/frame_textures"});
            const auto filter          = args.value("filter", std::string {});
            const auto camera          = args.value("camera", std::string {});
            const auto renderer        = args.value("renderer", std::string {});
            const int  maxTextures     = std::clamp(args.value("maxTextures", 64), 1, 512);
            const auto captureFrames = static_cast<uint32_t>(std::clamp(args.value("captureFrames", 4), 1, 120));
            const auto maxPreviewExtent = static_cast<uint32_t>(args.value("maxPreviewExtent", 0));
            auto newestFrameIndex = [renderService]() -> uint64_t {
                const auto* profiler = renderService->runtimeProfiler();
                if (!profiler || profiler->history().empty())
                    return 0;
                return profiler->history().back().frameIndex;
            };

            if (call && !call->frameCaptureRequested)
            {
                auto frameCamera =
                    makeMcpFrameGraphCamera(worldService->world(), renderer, backendService->backbuffer().getExtent());
                if (!frameCamera)
                    return toolError("no scene camera is available for MCP texture dump");
                if (!camera.empty())
                    frameCamera->name = camera;
                call->frameCaptureCameraName = frameCamera->name;
                cameraService->removeManualCamerasByName(call->frameCaptureCameraName);
                cameraService->addManualCamera(*frameCamera);
                call->frameCaptureRequested  = true;
                call->frameCaptureStartFrame = newestFrameIndex();
                call->frameCaptureStartedAt  = std::chrono::steady_clock::now();
                renderService->requestFrameGraphTextureDumpCapture(captureFrames, maxPreviewExtent, filter, camera, renderer);
                call->defer = true;
                return {};
            }
            if (call)
            {
                ++call->frameCapturePolls;
                const auto currentFrame = newestFrameIndex();
                const bool renderedAfterRequest = currentFrame > call->frameCaptureStartFrame ||
                                                  call->frameCapturePolls >= 2u;
                if (!renderedAfterRequest)
                {
                    call->defer = true;
                    return {};
                }
            }
            else
            {
                auto frameCamera =
                    makeMcpFrameGraphCamera(worldService->world(), renderer, backendService->backbuffer().getExtent());
                if (frameCamera)
                {
                    if (!camera.empty())
                        frameCamera->name = camera;
                    cameraService->removeManualCamerasByName(frameCamera->name);
                    cameraService->addManualCamera(*frameCamera);
                }
                renderService->requestFrameGraphTextureDumpCapture(captureFrames, maxPreviewExtent, filter, camera, renderer);
            }

            int pendingMatches = 0;
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (textureMatchesFilter(texture, filter, camera, renderer))
                    ++pendingMatches;
            }
            if (call && pendingMatches == 0)
            {
                const auto elapsed = std::chrono::steady_clock::now() - call->frameCaptureStartedAt;
                if (elapsed < std::chrono::seconds(4))
                {
                    call->defer = true;
                    return {};
                }
            }

            std::error_code ec;
            std::filesystem::create_directories(outputDirectory, ec);
            if (ec)
                return toolError("failed to create output directory '" + outputDirectory + "': " + ec.message());

            nlohmann::json dumped = nlohmann::json::array();
            nlohmann::json failed = nlohmann::json::array();
            int            considered = 0;
            int            written    = 0;
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (!textureMatchesFilter(texture, filter, camera, renderer))
                    continue;
                ++considered;
                if (written >= maxTextures)
                    continue;
                if (!texture.texture || !texture.capturable)
                {
                    failed.push_back({{"name", texture.name},
                                      {"key", texture.key},
                                      {"reason", texture.texture ? "texture is not capturable" : "texture pointer is null"}});
                    continue;
                }
                if (!static_cast<bool>(*texture.texture))
                {
                    failed.push_back({{"name", texture.name},
                                      {"key", texture.key},
                                      {"reason", "texture handle is invalid"},
                                      {"format", std::string(vultra::rhi::toString(texture.texture->getPixelFormat()))},
                                      {"extent",
                                       {{"width", texture.texture->getExtent().width},
                                        {"height", texture.texture->getExtent().height}}}});
                    continue;
                }

                const auto stem = safeFileStem(texture.camera + "_" + texture.renderer + "_" + texture.name + "_" +
                                               texture.key + "_" + std::to_string(texture.layer));
                const auto path = std::filesystem::path(outputDirectory) / (stem + ".png");
                bool        ok = false;
                std::string error;
                try
                {
                    const auto pixels = backendService->renderDevice().readTextureRGBA8(*texture.texture);
                    if (!pixels)
                    {
                        error = "RGBA8 preview texture readback failed";
                        error += " format=" + std::string(vultra::rhi::toString(texture.texture->getPixelFormat()));
                        error += " size=" + std::to_string(texture.texture->getSize());
                    }
                    else if (pixels->size() < static_cast<size_t>(texture.extent.width) * texture.extent.height * 4u)
                    {
                        error = "RGBA8 preview texture readback returned incomplete data";
                    }
                    else
                    {
                        ok = stbi_write_png(path.string().c_str(),
                                            static_cast<int>(texture.extent.width),
                                            static_cast<int>(texture.extent.height),
                                            4,
                                            pixels->data(),
                                            static_cast<int>(texture.extent.width * 4u)) != 0;
                        if (!ok)
                            error = "failed to write png";
                    }
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                }
                if (ok)
                {
                    ++written;
                    dumped.push_back({{"file", path.generic_string()},
                                      {"camera", texture.camera},
                                      {"renderer", texture.renderer},
                                      {"name", texture.name},
                                      {"key", texture.key},
                                      {"resourceKey", texture.resourceKey},
                                      {"layer", texture.layer},
                                      {"extent", {{"width", texture.extent.width}, {"height", texture.extent.height}}},
                                      {"format", std::string(vultra::rhi::toString(texture.format))}});
                }
                else
                {
                    failed.push_back({{"name", texture.name},
                                      {"key", texture.key},
                                      {"file", path.generic_string()},
                                      {"reason", error.empty() ? "render device failed to save texture" : error}});
                }
            }

            if (call && !call->frameCaptureCameraName.empty())
                cameraService->removeManualCamerasByName(call->frameCaptureCameraName);

            return toolJson({{"ok", true},
                             {"captureEnabled", true},
                             {"outputDirectory", outputDirectory},
                             {"matchedTextures", considered},
                             {"dumpedTextures", written},
                             {"captureAllTextures", true},
                             {"textures", std::move(dumped)},
                             {"failed", std::move(failed)}});
        }

        if (name == "vultra.runtime.reload_pipeline")
        {
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            if (!renderService)
                return toolError("render service is unavailable");
            const auto asset = args.value("asset", ctx.state.currentEditingRenderGraph);
            const auto rendererKey = args.value("rendererKey", std::string {});
            const bool ok = asset.empty() ? renderService->reloadRenderPipeline() :
                                            renderService->reloadRenderPipeline(asset, rendererKey);
            return toolJson({{"ok", ok}, {"asset", asset}, {"rendererKey", rendererKey}});
        }

        if (name == "vultra.runtime.capture_frame")
        {
            auto* frameDebugger = ctx.services ? ctx.services->tryGet<vultra::IFrameDebuggerService>() : nullptr;
            if (!frameDebugger)
                return toolError("frame debugger service is unavailable");
            if (!frameDebugger->isRenderDocEnabled() || !frameDebugger->isAvailable())
            {
                return toolJson({{"ok", false},
                                 {"available", frameDebugger->isAvailable()},
                                 {"renderDocEnabled", frameDebugger->isRenderDocEnabled()},
                                 {"error", "RenderDoc capture is unavailable"}});
            }
            frameDebugger->captureSingleFrame();
            return toolJson({{"ok", true}, {"captureCount", frameDebugger->getCaptureCount()}});
        }


        return nullptr;
    }
} // namespace vultra_app
