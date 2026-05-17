#include "vultra/function/rendering/render_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/imgui_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/shader_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/gtc/packing.hpp>

#include <vbase/core/exe_path.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>
#ifndef NDEBUG
#include <fstream>
#endif

namespace vultra
{
    namespace
    {
        thread_local rhi::BuiltinProfilerGpuScopeContext g_CurrentBuiltinProfilerGpuScopeContext {};

        void clearColorTarget(rhi::CommandBuffer&        cb,
                              rhi::Texture&              target,
                              const rhi::Rect2D&         area,
                              const std::optional<rhi::ClearValue>& clearValue,
                              const bool                 enableMultiview,
                              const uint32_t             multiviewMask)
        {
            rhi::FramebufferInfo clearFbInfo {
                .area             = area,
                .layers           = enableMultiview ? 2u : 1u,
                .viewMask         = enableMultiview ? multiviewMask : 0u,
                .colorAttachments = {rhi::AttachmentInfo {
                    .target     = &target,
                    .clearValue = clearValue.has_value() ? clearValue : std::optional<rhi::ClearValue> {glm::vec4 {0, 0, 0, 1}},
                }},
            };

            rhi::prepareForAttachment(cb, target, false);
            cb.beginRendering(clearFbInfo);
            cb.endRendering();
        }

        float effectiveGaussianAutomaticClodLevel(const GaussianSplatRenderSettings& settings)
        {
            if (!settings.foveatedClodActive())
                return std::clamp(settings.clodLevel, 0.01f, 1.0f);

            const glm::vec3 levels {
                std::clamp(settings.foveatedRingLevels.x, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.y, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.z, 0.0f, 1.0f),
            };
            return std::clamp(std::max(levels.x, std::max(levels.y, levels.z)), 0.01f, 1.0f);
        }

        uint32_t effectiveGaussianLodBudget(const GaussianSplatRenderSettings& settings, const uint32_t totalSplatCount)
        {
            // Baseline consumes the full table. Ordered CLOD consumes a prefix of
            // the table that vasset already sorted by importance at import time.
            if (!settings.lodBudgetEnabled())
                return totalSplatCount;

            // An explicit budget is useful for repeatable profiling. With budget 0
            // the UI exposes clodLevel as the paper-style continuous LOD fraction.
            if (settings.lodBudget > 0u)
                return std::min(totalSplatCount, settings.lodBudget);
            const float clodLevel = effectiveGaussianAutomaticClodLevel(settings);
            return std::min(totalSplatCount,
                            std::max(1u, static_cast<uint32_t>(
                                             std::ceil(static_cast<float>(totalSplatCount) * clodLevel))));
        }

        void applyGaussianSplatFoveatedClodSettings(resource::GpuSceneView&            gpuSceneView,
                                                    const GaussianSplatRenderSettings& settings)
        {
            const auto layers = settings.foveatedLayers();
            gpuSceneView.setGeneralGaussianSplatFoveatedClod(settings.foveatedClodActive(),
                                                             settings.foveatedLayeredCompositeActive(),
                                                             settings.foveatedGaze,
                                                             glm::vec2 {layers[0].eccentricityDegrees,
                                                                        layers[1].eccentricityDegrees},
                                                             glm::vec3 {layers[0].lodLevel,
                                                                        layers[1].lodLevel,
                                                                        layers[2].lodLevel},
                                                             glm::vec3 {layers[0].resolutionScale,
                                                                        layers[1].resolutionScale,
                                                                        layers[2].resolutionScale},
                                                             std::max(settings.foveatedTransitionDegrees, 0.0f));
        }

        void resetGaussianSplatIndirectBuffer(rhi::RenderDevice& rd, rhi::DrawIndirectBuffer& buffer)
        {
            std::vector<rhi::DrawIndirectCommand> indirect(1u);
            indirect[0].type          = rhi::DrawIndirectType::eNonIndexed;
            indirect[0].count         = 4u;
            indirect[0].instanceCount = 0u;
            indirect[0].first         = 0u;
            indirect[0].vertexOffset  = 0;
            indirect[0].firstInstance = 0u;
            rd.uploadDrawIndirect(buffer, indirect);
        }

        void resetGaussianSplatIndirectBuffers(rhi::RenderDevice& rd, resource::GpuSceneView& gpuSceneView)
        {
            if (gpuSceneView.generalGaussianSplatIndirectBuffer.has_value())
                resetGaussianSplatIndirectBuffer(rd, gpuSceneView.generalGaussianSplatIndirectBuffer.value());

            for (auto& buffer : gpuSceneView.generalGaussianSplatFoveatedIndirectBuffers)
            {
                if (buffer.has_value())
                    resetGaussianSplatIndirectBuffer(rd, buffer.value());
            }
        }

        bool gaussianSplatSelectionSettingsDirty(const GaussianSplatRenderSettings& current,
                                                 const GaussianSplatRenderSettings& applied)
        {
            return current.lodBudget != applied.lodBudget ||
                   current.clodLevel != applied.clodLevel ||
                   current.foveatedClodEnabled != applied.foveatedClodEnabled ||
                   current.foveatedRenderMode != applied.foveatedRenderMode ||
                   current.foveatedGaze != applied.foveatedGaze ||
                   current.foveatedRingDegrees != applied.foveatedRingDegrees ||
                   current.foveatedRingLevels != applied.foveatedRingLevels ||
                   current.foveatedResolutionScales != applied.foveatedResolutionScales ||
                   current.foveatedTransitionDegrees != applied.foveatedTransitionDegrees;
        }

        void updateGaussianSplatFoveatedBudgetController(GaussianSplatRenderSettings& settings,
                                                         const double                 gpuFrameMs)
        {
            if (!settings.foveatedClodActive() || !settings.foveatedBudgetControllerEnabled || gpuFrameMs <= 0.0)
                return;

            const float targetMs = std::max(settings.foveatedTargetFrameMs, 0.1f);
            const float maxStep = std::clamp(settings.foveatedBudgetAdjustRate, 0.001f, 0.25f);
            const float error = static_cast<float>((targetMs - gpuFrameMs) / targetMs);
            if (std::abs(error) < 0.03f)
                return;

            const float signedStep = std::clamp(error * 0.5f, -maxStep, maxStep);
            auto adjust = [signedStep](float value, const float floorValue) {
                return std::clamp(value + signedStep * std::max(value, 0.1f), floorValue, 1.0f);
            };

            settings.foveatedRingLevels.z = adjust(settings.foveatedRingLevels.z, 0.01f);
            settings.foveatedRingLevels.y = adjust(settings.foveatedRingLevels.y, settings.foveatedRingLevels.z);
            if (error < -0.35f)
                settings.foveatedRingLevels.x = adjust(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
            else
                settings.foveatedRingLevels.x = std::max(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
        }

        void rebuildGaussianSplatOrderedClodPrefixSources(resource::GpuSceneView&                       gpuSceneView,
                                                          const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                          const resource::GpuResourcePool&                 pool,
                                                          RuntimeProfiler&                                 profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            struct OrderedSource
            {
                uint32_t rankNumerator {0};
                uint32_t pointCount {1};
                uint32_t drawIndex {0};
                uint32_t rank {0};
                resource::GpuGeneralGaussianSplatSelectedSource selection {};
            };

            RuntimeProfiler::Scope scope {profiler, "GaussianCLOD::BuildPrefix"};
            const bool             singleDraw = gpuSceneView.generalGaussianSplatDraws.size() <= 1u;
            std::vector<OrderedSource> orderedSources;
            if (!singleDraw)
            {
                uint32_t reserveCount = 0u;
                for (const auto& splatInst : gaussianSplats)
                {
                    if (splatInst.splatIndex < pool.gaussianSplats.size())
                        reserveCount += pool.gaussianSplats[splatInst.splatIndex].pointCount;
                }
                orderedSources.reserve(reserveCount);
            }

            uint32_t drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&  drawRecord      = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset  = drawRecord.pointOffset;
                const uint32_t rankCount     = gpuSplat.pointCount;

                for (uint32_t rank = 0u; rank < rankCount; ++rank)
                {
                    const uint32_t localPoint = rank;

                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;

                    if (singleDraw)
                    {
                        gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    }
                    else
                    {
                        orderedSources.push_back(OrderedSource {
                            .rankNumerator = rank + 1u,
                            .pointCount    = gpuSplat.pointCount,
                            .drawIndex     = drawIndex,
                            .rank          = rank,
                            .selection     = selection,
                        });
                    }
                }

                ++drawIndex;
            }

            if (!singleDraw)
            {
                std::stable_sort(orderedSources.begin(), orderedSources.end(), [](const auto& a, const auto& b) {
                    const uint64_t lhs =
                        static_cast<uint64_t>(a.rankNumerator) * static_cast<uint64_t>(b.pointCount);
                    const uint64_t rhs =
                        static_cast<uint64_t>(b.rankNumerator) * static_cast<uint64_t>(a.pointCount);
                    if (lhs != rhs)
                        return lhs < rhs;
                    if (a.drawIndex != b.drawIndex)
                        return a.drawIndex < b.drawIndex;
                    return a.rank < b.rank;
                });

                gpuSceneView.generalGaussianSplatSelectedSources.reserve(orderedSources.size());
                for (const auto& source : orderedSources)
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(source.selection);
            }
        }

        void rebuildGaussianSplatSelectedSources(resource::GpuSceneView&                       gpuSceneView,
                                                 const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                 const resource::GpuResourcePool&                 pool,
                                                 GaussianSplatFrameStats&                         stats,
                                                 RuntimeProfiler&                                 profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            // Baseline still builds a selected-source table so the preprocess
            // shader can share one path with Ordered CLOD.
            RuntimeProfiler::Scope scope {profiler, "GaussianSplat::BuildRawSelection"};
            uint32_t               drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&  drawRecord      = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset  = drawRecord.pointOffset;
                for (uint32_t localPoint = 0u; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    ++stats.lodSelectedRawSplats;
                }

                ++drawIndex;
            }
        }
    } // namespace

    void RenderWorldCooker::cook(World& world, IAssetService& assets, RenderWorld& out)
    {
        out.clear();

        auto& reg = world.registry();

        auto view = reg.view<IDComponent, TransformComponent, MeshComponent>();
        out.instances.reserve(view.size_hint());
        for (auto e : view)
        {
            const auto& id   = view.get<IDComponent>(e);
            const auto& tr   = view.get<TransformComponent>(e);
            const auto& mesh = view.get<MeshComponent>(e);
            if (auto* status = reg.try_get<EntityStatusComponent>(e); status && (!status->active || !status->visible))
                continue;

            auto h = assets.loadMeshSync(mesh.mesh);
            if (!h.ready())
                continue;

            RenderInstance inst {};
            inst.entity      = id.uuid;
            inst.meshIndex   = h.gpuIndex();
            inst.worldMatrix = tr.worldMatrix;
            out.instances.push_back(inst);
        }

        auto splatView = reg.view<IDComponent, TransformComponent, GaussianSplatComponent>();
        out.gaussianSplats.reserve(splatView.size_hint());
        for (auto e : splatView)
        {
            const auto& id    = splatView.get<IDComponent>(e);
            const auto& tr    = splatView.get<TransformComponent>(e);
            const auto& splat = splatView.get<GaussianSplatComponent>(e);
            if (auto* status = reg.try_get<EntityStatusComponent>(e); status && (!status->active || !status->visible))
                continue;

            auto h = assets.loadGaussianSplatSync(splat.gaussianSplat);
            if (!h.ready())
                continue;

            RenderGaussianSplatInstance inst {};
            inst.entity      = id.uuid;
            inst.splatIndex  = h.gpuIndex();
            inst.worldMatrix = tr.worldMatrix;
            out.gaussianSplats.push_back(inst);
        }
    }

    bool RenderSystem::onInit()
    {
        VULTRA_CORE_INFO("[RenderSystem] Initializing...");

#if defined(__ANDROID__)
        // Android paths currently rely on the CPU-driven renderer.
        m_EnableGpuDrivenMeshletPipeline = false;
#endif

        VULTRA_CORE_TRACE("[RenderSystem] Getting render backend service");
        auto& backendService = ctx().services.require<IRenderBackendService>();

        VULTRA_CORE_TRACE("[RenderSystem] Getting window service");
        auto& windowService = ctx().services.require<IWindowService>();

        VULTRA_CORE_TRACE("[RenderSystem] Creating transient resources");
        m_TransientResources = createScope<framegraph::TransientResources>(backendService.renderDevice());

        VULTRA_CORE_TRACE("[RenderSystem] Initializing renderers");
        for (auto& [key, renderer] : m_Renderers)
        {
            VULTRA_CORE_TRACE("[RenderSystem]     Initializing renderer: {}", key);
            Services services = ctx().services;
            renderer->setupServices(services);
            renderer->init();
        }

        VULTRA_CORE_TRACE("[RenderSystem] Initializing samplers");
        m_Samplers["default"] = backendService.renderDevice().getSampler(rhi::SamplerInfo {});
        m_Samplers["linear"]  = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eLinear, .minFilter = rhi::TexelFilter::eLinear});
        m_Samplers["nearest"] = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eNearest, .minFilter = rhi::TexelFilter::eNearest});

        VULTRA_CORE_TRACE("[RenderSystem] Providing IRenderService");
        ctx().services.provide<IRenderService>(this);

        VULTRA_CORE_INFO("[RenderSystem] Initialized!");

        return true;
    }

    void RenderSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[RenderSystem] Shutting down");

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.renderDevice().waitIdle();

        m_GpuSceneViewBack.clear();
        m_GpuSceneViewFront.clear();
        m_GpuSceneDatabaseBack.clear();
        m_GpuSceneDatabaseFront.clear();
        m_GpuSceneDirtyTracker.reset();

        for (auto& [key, renderer] : m_Renderers)
            renderer = nullptr;
        m_Renderers.clear();

        m_TransientResources.reset();

        m_FrameResources.clear();
    }

    void RenderSystem::registerRenderer(Ref<Renderer> renderer)
    {
        if (!renderer)
            return;
        if (m_Renderers.contains(std::string(renderer->name())))
            return;
        m_Renderers[std::string(renderer->name())] = renderer;
    }

    Ref<Renderer> RenderSystem::resolveRenderer(const RenderCamera& cam) const
    {
        if (auto it = m_Renderers.find(cam.rendererKey); it != m_Renderers.end())
            return it->second;

        if (auto it2 = m_Renderers.find(m_DefaultRendererKey); it2 != m_Renderers.end())
            return it2->second;

        return nullptr;
    }

    void RenderSystem::onResize(uint32_t width, uint32_t height)
    {
        for (auto& [key, renderer] : m_Renderers)
        {
            if (renderer)
                renderer->onResize(width, height);
        }
    }

    void RenderSystem::renderFrame()
    {
        const auto renderFrameCpuStart = std::chrono::steady_clock::now();

        auto& backendService     = ctx().services.require<IRenderBackendService>();
        auto& worldService       = ctx().services.require<IWorldService>();
        auto& camService         = ctx().services.require<ICameraService>();
        auto& gpuResourceService = ctx().services.require<IGpuResourceService>();
        auto& assetService       = ctx().services.require<IAssetService>();
        auto& shaderService      = ctx().services.require<IShaderService>();
        auto& window             = ctx().services.require<IWindowService>().window();

        // Optional ImGui service for rendering ImGui on top of frame.
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        // Optional frame debugger service for GPU capture.
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();

        auto& rd = backendService.renderDevice();

        m_RuntimeProfiler.beginFrame(m_FrameCounter);
        m_RuntimeProfiler.setVsyncEnabled(ctx().config.render.vSyncConfig != rhi::VerticalSync::eDisabled);
        rhi::CommandBuffer::resetFrameStats();

        // Begin frame first so downstream systems can consume per-frame backend state (e.g. XR eye views).
        if (!backendService.beginFrame())
        {
            m_SkipRender = true;
            m_RuntimeProfiler.endFrame();
            return;
        }

        auto& cb = backendService.commandBuffer();
        RuntimeProfiler::Scope scopeRenderFrame {m_RuntimeProfiler, "RenderSystem::renderFrame"};
        rd.beginFrameGpuQuery(cb);

        // Default target for cameras without explicit RT
        auto& defaultTarget = backendService.backbuffer();

        if (frameDebuggerService)
        {
            frameDebuggerService->captureStart();
        }

        World&     world = worldService.world();
        const auto cams  = camService.cameras();

        // Asset upload/update stage (main thread)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "AssetService::update"};
            assetService.update(m_FrameCounter);
        }
        // Cook render instances
        RenderWorldCooker cooker {};
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "RenderWorldCooker::cook"};
            cooker.cook(world, assetService, m_RenderWorldBack);
        }
        m_RenderWorldBack.frameIndex = m_FrameCounter;

        const bool gaussianOrderedClodMode = m_GaussianSplatSettings.orderedClodEnabled();

        const uint64_t resourceRevision = gpuResourceService.contentRevision();
        const auto&    pool             = gpuResourceService.pool();

        uint32_t maxGeneralGaussianSplatPoints = 0;
        uint32_t maxGeneralGaussianSplatSourceCount = 0;
        for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
        {
            if (splatInst.splatIndex >= pool.gaussianSplats.size())
                continue;
            const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
            maxGeneralGaussianSplatPoints += gpuSplat.pointCount;
            maxGeneralGaussianSplatSourceCount += gpuSplat.pointCount;
        }

        GaussianSplatFrameStats gaussianStats {};
        gaussianStats.frameIndex                       = m_FrameCounter;
        gaussianStats.baselineMode                     = m_GaussianSplatSettings.baselineMode;
        gaussianStats.foveatedRenderMode               = m_GaussianSplatSettings.foveatedRenderMode;
        gaussianStats.lodBudgetEnabled                 = m_GaussianSplatSettings.lodBudgetEnabled();
        gaussianStats.foveatedClodEnabled              = m_GaussianSplatSettings.foveatedClodActive();
        gaussianStats.foveatedLayeredCompositeEnabled = m_GaussianSplatSettings.foveatedLayeredCompositeActive();
        gaussianStats.foveatedBudgetControllerEnabled = m_GaussianSplatSettings.foveatedBudgetControllerEnabled;
        gaussianStats.lodBudget                        = m_GaussianSplatSettings.lodBudget;
        gaussianStats.foveatedRingLevels               = m_GaussianSplatSettings.foveatedRingLevels;
        gaussianStats.foveatedResolutionScales         = m_GaussianSplatSettings.foveatedResolutionScales;
        gaussianStats.foveatedRingDegrees              = m_GaussianSplatSettings.foveatedRingDegrees;
        gaussianStats.foveatedTargetFrameMs            = m_GaussianSplatSettings.foveatedTargetFrameMs;
        gaussianStats.splatAssets                      = static_cast<uint32_t>(m_RenderWorldBack.gaussianSplats.size());
        gaussianStats.totalSplats                      = maxGeneralGaussianSplatPoints;

        const bool gaussianModeSettingsDirty =
            m_GaussianSplatSettings.baselineMode != m_AppliedGaussianSplatSettings.baselineMode;
        const bool gaussianSelectionSettingsDirty =
            gaussianSplatSelectionSettingsDirty(m_GaussianSplatSettings, m_AppliedGaussianSplatSettings);
        const bool gpuSceneDirty =
            gaussianModeSettingsDirty ||
            m_GpuSceneDirtyTracker.shouldRebuild(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        const bool gaussianSelectionDirty = gaussianOrderedClodMode && gaussianSelectionSettingsDirty;

        // Build GPU scene database + per-view draw state.
        //
        // Database layer:
        // - stable pointer to global resource pool
        // - scene/instance tables
        //
        // View layer:
        // - draw table
        // - indirect commands
        if (gpuSceneDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::rebuild"};
            auto        packGaussianCovariance = [](const glm::uvec4 packed) {
                const glm::vec2 p0 = glm::unpackHalf2x16(packed.x);
                const glm::vec2 p1 = glm::unpackHalf2x16(packed.y);
                const glm::vec2 p2 = glm::unpackHalf2x16(packed.z);

                glm::mat3 sigma(0.0f);
                sigma[0][0] = p0.x;
                sigma[1][0] = p0.y;
                sigma[0][1] = p0.y;
                sigma[2][0] = p1.x;
                sigma[0][2] = p1.x;
                sigma[1][1] = p1.y;
                sigma[2][1] = p2.x;
                sigma[1][2] = p2.x;
                sigma[2][2] = p2.y;
                return sigma;
            };
            auto repackGaussianCovariance = [](const glm::mat3& sigma) {
                return glm::uvec4 {
                    glm::packHalf2x16(glm::vec2(sigma[0][0], sigma[1][0])),
                    glm::packHalf2x16(glm::vec2(sigma[2][0], sigma[1][1])),
                    glm::packHalf2x16(glm::vec2(sigma[2][1], sigma[2][2])),
                    0u,
                };
            };

            m_GpuSceneDatabaseBack.beginFrame(pool);
            m_GpuSceneDatabaseBack.instances.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.transforms.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.rebuildMeshTableFromResources();

            // Keep CPU staging mirrors even though the current render path is still
            // CPU-driven. The upcoming GPU-driven cluster pipeline will consume the
            // same scene database buffers directly.
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                const uint32_t transformIndex = m_GpuSceneDatabaseBack.pushTransform(inst.worldMatrix);

                resource::GpuInstance gpuInst {};
                gpuInst.meshIndex      = inst.meshIndex;
                gpuInst.materialIndex  = inst.materialIndex;
                gpuInst.transformIndex = transformIndex;
                gpuInst.flags          = 0;
                m_GpuSceneDatabaseBack.pushInstance(gpuInst);
            }
            m_GpuSceneDatabaseBack.uploadSceneTables(rd, cb);

            uint32_t maxMeshletDraws = 0;
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                if (inst.meshIndex >= pool.meshes.size())
                    continue;
                maxMeshletDraws += pool.meshes[inst.meshIndex].meshletCount;
            }

            if (m_EnableGpuDrivenMeshletPipeline)
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eGpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
            }
            else
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eCpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
                m_GpuSceneViewBack.ensureVisibleMeshletBuffers(rd);
                m_GpuSceneViewBack.draws.reserve(maxMeshletDraws);

                for (uint32_t instanceIndex = 0;
                     instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                     ++instanceIndex)
                {
                    const auto& inst = m_RenderWorldBack.instances[instanceIndex];
                    if (inst.meshIndex >= pool.meshes.size())
                        continue;
                    if (instanceIndex >= m_GpuSceneDatabaseBack.instances.size())
                        continue;

                    const auto& mesh    = pool.meshes[inst.meshIndex];
                    if (mesh.meshletCount == 0)
                        continue;

                    for (uint32_t localMeshlet = 0; localMeshlet < mesh.meshletCount; ++localMeshlet)
                    {
                        const uint32_t globalMeshletIndex = mesh.meshletOffset + localMeshlet;
                        if (globalMeshletIndex >= pool.meshlets.cpuMeshlets.size())
                            continue;

                        const auto& meshlet = pool.meshlets.cpuMeshlets[globalMeshletIndex];

                        resource::GpuDrawRecord dr;
                        dr.primitiveIndex    = globalMeshletIndex;
                        dr.materialIndex     = meshlet.materialIndex;
                        dr.vertexStrideBytes = mesh.vertexStrideBytes;
                        dr.flags             = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eMeshlet);
                        dr.vertexAddress     = pool.geometry.vertexBytesAddress;
                        dr.instanceIndex     = instanceIndex;
                        dr.padding0          = 0;
                        dr.model             = inst.worldMatrix;
                        m_GpuSceneViewBack.pushMeshletDraw(std::move(dr));
                    }
                }

                std::stable_sort(m_GpuSceneViewBack.draws.begin(), m_GpuSceneViewBack.draws.end(), [](const auto& a, const auto& b) {
                    if (a.materialIndex != b.materialIndex)
                        return a.materialIndex < b.materialIndex;
                    return a.primitiveIndex < b.primitiveIndex;
                });

                m_GpuSceneViewBack.uploadDraws(rd, cb);
                m_GpuSceneViewBack.buildIndirectFromDraws(pool);
                m_GpuSceneViewBack.uploadIndirect(rd);
            }

            m_GpuSceneViewBack.generalGaussianSplatDraws.clear();
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatSelectedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatDirectPrefix = false;
            m_GpuSceneViewBack.generalGaussianSplatDraws.reserve(m_RenderWorldBack.gaussianSplats.size());
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.reserve(maxGeneralGaussianSplatSourceCount);

            for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const uint32_t drawIndex = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
                const uint32_t pointBase = gpuSplat.pointOffset;
                const uint32_t shBaseStride = std::max(gpuSplat.shRestCoeffCount, 1u);
                const uint32_t rawSourceOffset =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());

                resource::GpuGeneralGaussianSplatDrawRecord drawRecord {};
                drawRecord.splatIndex  = splatInst.splatIndex;
                drawRecord.pointOffset = rawSourceOffset;
                drawRecord.pointCount  = gpuSplat.pointCount;
                drawRecord.shDegree    = static_cast<uint32_t>(std::max(gpuSplat.shDegree, 0));
                // x: kernel size, y: cutoff scale, z: opacity scale, w: reserved sort order.
                drawRecord.params0 = glm::vec4 {0.3f,
                                                1.0f,
                                                1.0f,
                                                0.0f};
                drawRecord.model   = splatInst.worldMatrix;

                for (uint32_t localPoint = 0; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    const uint32_t globalPoint = pointBase + localPoint;
                    resource::GpuGeneralGaussianSplatPackedSource packed {};
                    if (globalPoint < pool.gaussianStorage.cpuCenters.size() &&
                        globalPoint < pool.gaussianStorage.cpuCovariances.size() &&
                        globalPoint < pool.gaussianStorage.cpuColors.size())
                    {
                        const glm::vec4 localCenter = pool.gaussianStorage.cpuCenters[globalPoint];
                        const uint32_t shOffset = globalPoint * shBaseStride;
                        const glm::uvec2 sh0 =
                            shOffset < pool.gaussianStorage.cpuSh.size() ?
                                pool.gaussianStorage.cpuSh[shOffset] :
                                glm::uvec2 {0u};

                        packed.posOpacity = glm::uvec4 {
                            std::bit_cast<uint32_t>(localCenter.x),
                            std::bit_cast<uint32_t>(localCenter.y),
                            std::bit_cast<uint32_t>(localCenter.z),
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                        };
                        packed.covariance0 = pool.gaussianStorage.cpuCovariances[globalPoint];
                        packed.colorSh0    = glm::uvec4 {
                            pool.gaussianStorage.cpuColors[globalPoint].x,
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                            sh0.x,
                            sh0.y,
                        };
                        packed.aux0 = glm::uvec4 {globalPoint, 0u, shOffset, 0u};
                    }
                    m_GpuSceneViewBack.pushGeneralGaussianSplatSource(packed);
                }

                m_GpuSceneViewBack.pushGeneralGaussianSplatDraw(drawRecord);
            }

            uint32_t selectedSourceCapacity = 0u;
            uint32_t activeGaussianSplats   = 0u;
            const uint32_t packedGaussianSources =
                static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());
            const bool gaussianDirectPrefix =
                gaussianOrderedClodMode && m_GpuSceneViewBack.generalGaussianSplatDraws.size() == 1u;
            gaussianStats.directPrefix = gaussianDirectPrefix;
            if (gaussianDirectPrefix)
            {
                selectedSourceCapacity = 0u;
                activeGaussianSplats =
                    std::min(packedGaussianSources,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else if (gaussianOrderedClodMode)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(m_GpuSceneViewBack,
                                                             m_RenderWorldBack.gaussianSplats,
                                                             pool,
                                                             m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats =
                    std::min(selectedSourceCapacity,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else
            {
                rebuildGaussianSplatSelectedSources(m_GpuSceneViewBack,
                                                    m_RenderWorldBack.gaussianSplats,
                                                    pool,
                                                    gaussianStats,
                                                    m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats = selectedSourceCapacity;
            }

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords        = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats     = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            m_GpuSceneViewBack.setGeneralGaussianSplatCaps(
                gaussianStats.drawRecords,
                packedGaussianSources,
                selectedSourceCapacity,
                activeGaussianSplats,
                maxVisibleGaussianSplats,
                gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(m_GpuSceneViewBack, m_GaussianSplatSettings);
            m_GpuSceneViewBack.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            m_GpuSceneViewBack.ensureGeneralGaussianSplatBuffers(rd);

            if (!m_GpuSceneViewBack.generalGaussianSplatDraws.empty() && m_GpuSceneViewBack.generalGaussianSplatDrawBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          m_GpuSceneViewBack.generalGaussianSplatDraws.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatPackedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatPackedSource),
                          m_GpuSceneViewBack.generalGaussianSplatPackedSources.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatSelectedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          m_GpuSceneViewBack.generalGaussianSplatSelectedSources.data());
            }

            if (m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer,
                          0,
                          sizeof(zeroArgs),
                          zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, m_GpuSceneViewBack);

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseBack;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewBack;
        }
        else
        {
            // Reuse previous snapshot when neither cooked world nor resource pool changed.
            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }

        if (!gpuSceneDirty && gaussianSelectionDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::gaussian_lod_selection"};
            auto& gpuSceneView = m_GpuSceneViewFront;

            uint32_t selectedSourceCapacity = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
            uint32_t activeGaussianSplats   = 0u;
            bool     uploadSelectedSources  = false;
            const bool gaussianDirectPrefix = gpuSceneView.generalGaussianSplatDirectPrefix;
            gaussianStats.directPrefix      = gaussianDirectPrefix;
            if (!gaussianDirectPrefix && selectedSourceCapacity < maxGeneralGaussianSplatPoints)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(gpuSceneView,
                                                             m_RenderWorldBack.gaussianSplats,
                                                             pool,
                                                             m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
                uploadSelectedSources = true;
            }

            const uint32_t activeBudgetSourceCount =
                gaussianDirectPrefix ? static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()) :
                                       selectedSourceCapacity;
            activeGaussianSplats =
                std::min(activeBudgetSourceCount,
                         effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
            gaussianStats.lodSelectedRawSplats = activeGaussianSplats;

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords        = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats     = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            gpuSceneView.setGeneralGaussianSplatCaps(
                gaussianStats.drawRecords,
                static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()),
                selectedSourceCapacity,
                activeGaussianSplats,
                maxVisibleGaussianSplats,
                gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(gpuSceneView, m_GaussianSplatSettings);
            gpuSceneView.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            gpuSceneView.ensureGeneralGaussianSplatBuffers(rd);

            if (uploadSelectedSources && !gpuSceneView.generalGaussianSplatSelectedSources.empty() &&
                gpuSceneView.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*gpuSceneView.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          gpuSceneView.generalGaussianSplatSelectedSources.data());
            }

            if (gpuSceneView.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*gpuSceneView.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (gpuSceneView.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*gpuSceneView.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, gpuSceneView);
        }

        m_GpuSceneDirtyTracker.markBuilt(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        if (gpuSceneDirty || gaussianSelectionDirty)
        {
            m_AppliedGaussianSplatSettings = m_GaussianSplatSettings;
        }

        std::swap(m_RenderWorldFront, m_RenderWorldBack);
        if (gpuSceneDirty)
        {
            std::swap(m_GpuSceneDatabaseFront, m_GpuSceneDatabaseBack);
            std::swap(m_GpuSceneViewFront, m_GpuSceneViewBack);
        }
        m_RenderWorldFront.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
        m_RenderWorldFront.gpuSceneView     = &m_GpuSceneViewFront;
        m_RenderWorldBack.gpuSceneDatabase  = &m_GpuSceneDatabaseBack;
        m_RenderWorldBack.gpuSceneView      = &m_GpuSceneViewBack;

        m_FrameResources.beginFrame(m_FrameCounter);
        {
            ImmediateResourceUploader frameUploader {m_FrameResources, rd};
            prepareFrameData(frameUploader, m_PreparedFrameData, m_FrameCounter, 0.0f, 0.0f);
        }

        ++m_FrameCounter;

        std::vector<size_t> cameraOrder(cams.size());
        std::iota(cameraOrder.begin(), cameraOrder.end(), 0u);
        std::stable_sort(cameraOrder.begin(), cameraOrder.end(), [&cams](size_t a, size_t b) {
            return cams[a].priority < cams[b].priority;
        });

        const bool supportsMultiview =
            HasFlagValues(rd.getFeatureReport().flags, rhi::RenderDeviceFeatureReportFlagBits::eMultiview);
        const auto xrEyeViews               = backendService.xrEyeViews();
        bool       skipRemainingStereoViews = false;
        bool       backbufferClearedThisFrame = false;
        m_RuntimeProfiler.setGpuScopeCpuFallback(rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU);

        m_RuntimeProfiler.setGpuScopeCallbacks(
            [this, &rd, &cb]() {
                if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0)
                    return uint64_t {0};

                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                {
                    return uint64_t {0};
                }

                // WebGPU compute encoders are kept open lazily. At a framegraph pass boundary the next
                // top-level scope may still observe the previous compute pass as active, which would
                // incorrectly suppress or mis-attribute the new pass timing.
                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                    g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle == 0 &&
                    g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0 &&
                    m_RuntimeProfiler.gpuScopeDepth() <= 1)
                {
                    rhi::WebGPUCommandBufferAccess::closeActiveComputePassForProfilingBoundary(cb);
                    g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle = cb.getCurrentRenderPassEncoderHandle();
                    g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle =
                        cb.getCurrentComputePassEncoderHandle();
                }

                // WebGPU fallback timestamps are pass-bound; ignore nested scopes inside an active pass.
                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                    (g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle != 0 ||
                     g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0))
                {
                    return uint64_t {0};
                }
                return rd.beginScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle);
            },
            [&rd](const uint64_t token) {
                if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0 || token == 0)
                    return;
                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                    return;
                rd.endScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle, token);
            },
            [&rd](const uint64_t token) {
                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                    return -1.0;
                return rd.consumeScopeGpuMs(token);
            });
        rhi::setBuiltinProfilerGpuScopeCallbacks(
            [](const rhi::BuiltinProfilerGpuScopeContext& ctx) { g_CurrentBuiltinProfilerGpuScopeContext = ctx; },
            [this](const rhi::BuiltinProfilerGpuScopeContext& ctx, const char* label) {
                g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                (void)m_RuntimeProfiler.beginGpuScope(label ? label : "GPU Scope");
            },
            [this](const rhi::BuiltinProfilerGpuScopeContext& ctx) {
                g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                m_RuntimeProfiler.endGpuScope();
            });

        // TODO: TimeSystem, for now use 0
        const fsec dt {0};
        static_cast<void>(dt);

        for (const size_t cameraIdx : cameraOrder)
        {
            RuntimeProfiler::Scope scopeCamera {m_RuntimeProfiler, "RenderCamera::execute"};
            const auto& cam = cams[cameraIdx];

            if (skipRemainingStereoViews && cam.isXRView && !cam.isXRPrimaryView)
                continue;
            if (!cam.isXRView || cam.isXRPrimaryView)
                skipRemainingStereoViews = false;

            auto renderer = resolveRenderer(cam);
            if (!renderer)
                continue;

            FrameGraph             fg {};
            FrameGraphBlackboard   bb {};
            FrameGraphDataRegistry dataRegistry {};
            const bool             useFrameGraph = renderer->usesFrameGraph();

            const bool canUseXrMultiview = supportsMultiview && cam.isXRView && cam.isXRPrimaryView &&
                                           cam.viewCount == 2u && m_RenderWorldFront.instances.empty() &&
                                           !xrEyeViews.empty() && xrEyeViews[0].stereoTarget;

            rhi::Texture* target =
                canUseXrMultiview ? xrEyeViews[0].stereoTarget : (cam.target ? cam.target : &defaultTarget);
            if (!target)
                continue;

            const bool isBackbufferTarget = !cam.isXRView && cam.target == nullptr && target == &defaultTarget;
            const bool useWindowContentArea =
                isBackbufferTarget && window.platformType() == os::Window::PlatformType::eAndroidNativeWindow;
            const rhi::Rect2D renderArea = useWindowContentArea ?
                                               window.getContentArea() :
                                               rhi::Rect2D {.offset = {0, 0}, .extent = target->getExtent()};

            RenderView view {
                .renderWorld          = &m_RenderWorldFront,
                .camera               = &cam,
                .target               = target,
                .extent               = renderArea.extent,
                .clearValue           = cam.clearValue,
                .enableMultiview      = canUseXrMultiview,
                .multiviewMask        = canUseXrMultiview ? 0x3u : 0u,
                .multiviewCameras     = {&cam, nullptr},
                .multiviewCameraCount = canUseXrMultiview ? 2u : 0u,
                .gpuSceneDatabase     = m_RenderWorldFront.gpuSceneDatabase,
                .gpuSceneView         = m_RenderWorldFront.gpuSceneView,
            };

            if (canUseXrMultiview)
            {
                const auto secondEyeIt = std::find_if(cameraOrder.begin(), cameraOrder.end(), [&](size_t idx) {
                    return cams[idx].isXRView && !cams[idx].isXRPrimaryView && cams[idx].viewCount == cam.viewCount;
                });
                if (secondEyeIt != cameraOrder.end())
                    view.multiviewCameras[1] = &cams[*secondEyeIt];
            }

            rhi::FramebufferInfo fbInfo {
                .area             = renderArea,
                .layers           = canUseXrMultiview ? 2u : 1u,
                .viewMask         = canUseXrMultiview ? 0x3u : 0u,
                .colorAttachments = {rhi::AttachmentInfo {.target = target, .clearValue = cam.clearValue}},
            };

            ViewRenderData viewData {
                .view            = view,
                .framebufferInfo = fbInfo,
            };

            // Fallback clear for backbuffer cameras.
            // This guarantees a deterministic background even when renderer contributes no color pass
            // (e.g. pure ImGui examples with no framegraph features).
            if (isBackbufferTarget && !backbufferClearedThisFrame)
            {
                clearColorTarget(cb,
                                 *target,
                                 renderArea,
                                 cam.clearValue,
                                 canUseXrMultiview,
                                 0x3u);
                backbufferClearedThisFrame = true;
            }

            {
                ImmediateResourceUploader immediateUploader {m_FrameResources, rd};
                prepareCameraData(immediateUploader, viewData, renderArea.extent, cam, rd.getBackendApi());
            }

            ImmediateRenderContext immediateCtx {
                .cb          = cb,
                .rd          = rd,
                .frame       = m_PreparedFrameData,
                .viewData    = viewData,
                .resourceSet = {},
            };

            rhi::prepareForAttachment(cb, *target, false);
            renderer->render(immediateCtx);
            if (useFrameGraph)
            {
                FrameGraphResourceUploader fgUploader {fg};
                prepareFrameData(fgUploader, m_PreparedFrameData, m_RenderWorldFront.frameIndex, 0.0f, 0.0f);
                prepareCameraData(fgUploader, viewData, renderArea.extent, cam, rd.getBackendApi());
                bb.add<FrameData>(m_PreparedFrameData.frameData);
                bb.add<CameraData>(viewData.cameraData);
            }

            if (useFrameGraph)
            {
                RuntimeProfiler::Scope scopeFrameGraphBuild {m_RuntimeProfiler, "FrameGraph::build"};
                FrameGraphBuildContext buildCtx {
                    .fg       = fg,
                    .bb       = bb,
                    .rd       = rd,
                    .data     = dataRegistry,
                    .frame    = m_PreparedFrameData,
                    .viewData = viewData,
                };

                // This sets up the frame graph using a feature renderer or a custom graph-aware renderer.
                rhi::prepareForAttachment(cb, *target, false);
                renderer->buildFrameGraph(buildCtx);
                fg.compile();

#ifndef NDEBUG
                {
                    const std::filesystem::path debugRoot = !ctx().config.writableRoot.empty() ?
                                                                std::filesystem::path(ctx().config.writableRoot) :
                                                                vbase::executable_dir();
                    const std::filesystem::path debugPath = debugRoot / "framegraph.dot";
                    std::ofstream               ofs(debugPath);
                    if (ofs.is_open())
                    {
                        ofs << fg;
                    }
                    else
                    {
                        VULTRA_CORE_WARN("[RenderSystem] Failed to write framegraph dot file: {}",
                                         debugPath.generic_string());
                    }
                }
#endif

                viewData.framebufferInfo = std::nullopt; // Clear framebuffer info for execution phase, will be set by
                                                         // FrameGraphTexture preRead callback if needed.
                FrameGraphExecContext frameGraphExecCtx {
                    .cb          = cb,
                    .rd          = rd,
                    .frame       = m_PreparedFrameData,
                    .viewData    = viewData,
                    .resourceSet = {},
                    .ext         = {.builtinShaderLib = &shaderService.builtinLibrary(), .samplers = m_Samplers},
                };

                {
                    RuntimeProfiler::Scope scopeFrameGraphExec {m_RuntimeProfiler, "FrameGraph::execute"};
                    FG_GPU_ZONE(cb);
                    fg.execute(&frameGraphExecCtx, m_TransientResources.get());
                }
            }

            if (canUseXrMultiview)
                skipRemainingStereoViews = true;

            // Optional ImGui rendering per non-XR camera
            if (imguiService && !cam.isXRView && cam.renderImGui)
            {
                imguiService->begin();
                renderer->onImGui();
                imguiService->end();

                rhi::prepareForAttachment(cb, *target, false);
                imguiService->render(cb, fbInfo);
            }
        }

        if (imguiService && backendService.isXREnabled() && backendService.isXRMirrorEnabled() && !xrEyeViews.empty())
        {
            for (const auto& eyeView : xrEyeViews)
            {
                if (!eyeView.target || !eyeView.mirrorTarget)
                    continue;

                rhi::prepareForReading(cb, *eyeView.target);
                cb.blit(*eyeView.target, *eyeView.mirrorTarget, rhi::TexelFilter::eLinear);
            }

            imguiService->begin();

            std::unordered_set<Renderer*> imguiRenderers;
            for (const size_t cameraIdx : cameraOrder)
            {
                auto renderer = resolveRenderer(cams[cameraIdx]);
                if (!renderer || imguiRenderers.contains(renderer.get()))
                    continue;
                imguiRenderers.insert(renderer.get());
                renderer->onImGui();
            }

            imguiService->end();

            for (const auto& xrEyeView : xrEyeViews)
            {
                if (xrEyeView.mirrorTarget)
                    rhi::prepareForReading(cb, *xrEyeView.mirrorTarget);
            }

            const rhi::Rect2D imguiArea = window.platformType() == os::Window::PlatformType::eAndroidNativeWindow ?
                                              window.getContentArea() :
                                              rhi::Rect2D {.offset = {0, 0}, .extent = defaultTarget.getExtent()};

            rhi::FramebufferInfo imguiFbInfo {
                .area             = imguiArea,
                .colorAttachments = {rhi::AttachmentInfo {.target = &defaultTarget}},
            };

            rhi::prepareForAttachment(cb, defaultTarget, false);
            imguiService->render(cb, imguiFbInfo);
        }

        // Stop issuing begin/end scope queries after rendering submission building is done,
        // but keep resolve callback alive so endFrame can harvest ready GPU samples.
        m_RuntimeProfiler.setGpuScopeCallbacks(
            []() { return uint64_t {0}; },
            [](const uint64_t) {},
            [&rd](const uint64_t token) { return rd.consumeScopeGpuMs(token); });

        m_TransientResources->update();
        rd.endFrameGpuQuery(cb);
        backendService.endFrame();

        const auto commandStats = rhi::CommandBuffer::consumeFrameStats();
        m_RuntimeProfiler.setCommandStats(commandStats.drawCalls,
                                          commandStats.dispatchCalls,
                                          commandStats.traceRaysCalls,
                                          commandStats.copyOps,
                                          commandStats.updateOps);
        const auto assetMemoryStats = assetService.memoryStats();
        const auto memoryStats      = rd.getMemoryStats();
        m_RuntimeProfiler.setMemoryStats(assetMemoryStats.cpuCacheBytes,
                                          memoryStats.cpuCacheBytes,
                                          memoryStats.gpuDeviceLocalBytes,
                                          memoryStats.gpuHostVisibleBytes);
        const double gpuFrameMs = rd.consumeGpuFrameMs();
        m_RuntimeProfiler.setGpuFrameMs(gpuFrameMs);
        const auto renderFrameCpuEnd = std::chrono::steady_clock::now();
        m_RuntimeProfiler.setCpuRenderMs(
            std::chrono::duration<double, std::milli>(renderFrameCpuEnd - renderFrameCpuStart).count());
        m_RuntimeProfiler.endFrame();
        updateGaussianSplatFoveatedBudgetController(m_GaussianSplatSettings, gpuFrameMs);
        rhi::setBuiltinProfilerGpuScopeCallbacks({}, {});
        m_RuntimeProfiler.setGpuScopeCallbacks({}, {}, {});

        if (frameDebuggerService)
            frameDebuggerService->captureEnd();
    }

    void RenderSystem::onPreRender() { m_SkipRender = false; }

    void RenderSystem::onRender() { renderFrame(); }

    void RenderSystem::onPostRender()
    {
        auto* imguiService = ctx().services.tryGet<IImGuiService>();
        if (imguiService)
            imguiService->postRender();
    }

    void RenderSystem::onPresent()
    {
        if (m_SkipRender)
            return;

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.present();
    }
} // namespace vultra
