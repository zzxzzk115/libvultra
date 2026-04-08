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
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <vbase/core/exe_path.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace vultra
{
    namespace
    {
        thread_local rhi::BuiltinProfilerGpuScopeContext g_CurrentBuiltinProfilerGpuScopeContext {};

        [[nodiscard]] uint32_t computeGaussianSplatPointLimit(const rhi::RenderDeviceLimits& limits)
        {
            uint64_t maxPoints = std::numeric_limits<uint32_t>::max();

            if (limits.maxStorageBufferBindingSize > 0u)
                maxPoints = std::min(maxPoints, limits.maxStorageBufferBindingSize / sizeof(uint32_t));
            if (limits.maxBufferSize > 0u)
                maxPoints = std::min(maxPoints, limits.maxBufferSize / sizeof(uint32_t));
            if (limits.maxComputeWorkgroupsPerDimension > 0u)
                maxPoints = std::min(maxPoints, static_cast<uint64_t>(limits.maxComputeWorkgroupsPerDimension) * 64u);

            return static_cast<uint32_t>(std::min<uint64_t>(maxPoints, std::numeric_limits<uint32_t>::max()));
        }

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
        out.splatInstances.reserve(splatView.size_hint());
        for (auto e : splatView)
        {
            const auto& id     = splatView.get<IDComponent>(e);
            const auto& tr     = splatView.get<TransformComponent>(e);
            const auto& gsplat = splatView.get<GaussianSplatComponent>(e);

            auto h = assets.loadGaussianSplatSync(gsplat.gaussianSplat);
            if (!h.ready())
                continue;

            RenderSplatInstance inst {};
            inst.entity      = id.uuid;
            inst.splatIndex  = h.gpuIndex();
            inst.worldMatrix = tr.worldMatrix;
            out.splatInstances.push_back(inst);
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

        const uint64_t resourceRevision     = gpuResourceService.contentRevision();
        const bool     gpuSceneDirty        =
            m_GpuSceneDirtyTracker.shouldRebuild(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);

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
            const auto& pool = gpuResourceService.pool();

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

            // Stage gaussian splat draws (GPU-driven: cull shader writes the indirect buffer).
            {
                const uint32_t        maxSplatDraws    = static_cast<uint32_t>(m_RenderWorldBack.splatInstances.size());
                uint32_t              totalSplatPoints = 0u;
                const bool            compatibilityWebGpu = rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU;
                const uint32_t        maxPointBudget      = compatibilityWebGpu ? computeGaussianSplatPointLimit(rd.getLimits()) : 0u;
                bool                  pointBudgetClamped   = false;

                m_GpuSceneViewBack.setGaussianSplatGpuDrivenCaps(maxSplatDraws);
                m_GpuSceneViewBack.ensureGaussianSplatDrawBuffer(rd);
                m_GpuSceneViewBack.gaussianSplatDraws.reserve(maxSplatDraws);
                m_GpuSceneGaussianSplatPointDrawIdScratch.clear();
                if (compatibilityWebGpu && maxPointBudget > 0u)
                    m_GpuSceneGaussianSplatPointDrawIdScratch.reserve(maxPointBudget);

                for (const auto& inst : m_RenderWorldBack.splatInstances)
                {
                    if (inst.splatIndex >= pool.gaussianSplats.size())
                        continue;

                    resource::GpuDrawRecord dr;
                    dr.primitiveIndex     = inst.splatIndex;
                    dr.materialIndex      = 0;
                    dr.vertexStrideBytes  = 0;
                    dr.flags              = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eGaussianSplat);
                    dr.instanceIndex      = totalSplatPoints;
                    dr.model              = inst.worldMatrix;
                    dr.padding0           = 0;
                    const uint32_t drawId = m_GpuSceneViewBack.pushGaussianSplatDraw(std::move(dr));

                    const uint32_t pointCount = pool.gaussianSplats[inst.splatIndex].pointCount;
                    uint32_t       emitCount  = pointCount;
                    if (compatibilityWebGpu && totalSplatPoints >= maxPointBudget)
                    {
                        emitCount         = 0u;
                        pointBudgetClamped = true;
                    }
                    else if (compatibilityWebGpu && pointCount > (maxPointBudget - totalSplatPoints))
                    {
                        emitCount         = maxPointBudget - totalSplatPoints;
                        pointBudgetClamped = true;
                    }

                    if (emitCount > 0u)
                    {
                        m_GpuSceneGaussianSplatPointDrawIdScratch.insert(
                            m_GpuSceneGaussianSplatPointDrawIdScratch.end(), emitCount, drawId);
                        totalSplatPoints += emitCount;
                    }
                }

                if (compatibilityWebGpu && pointBudgetClamped && !m_HasLoggedGaussianSplatPointClamp)
                {
                    VULTRA_CORE_WARN(
                        "[RenderSystem] Clamping gaussian splat staged points to {} due to device memory/dispatch limits",
                        maxPointBudget);
                    m_HasLoggedGaussianSplatPointClamp = true;
                }

                m_GpuSceneViewBack.uploadGaussianSplatDraws(rd, cb);
                m_GpuSceneViewBack.ensureGaussianSplatPointDrawIdBuffer(rd, totalSplatPoints);
                if (totalSplatPoints > 0u && m_GpuSceneViewBack.gaussianSplatPointDrawIdBuffer)
                {
                    cb.update(*m_GpuSceneViewBack.gaussianSplatPointDrawIdBuffer,
                              0,
                              static_cast<uint64_t>(m_GpuSceneGaussianSplatPointDrawIdScratch.size()) * sizeof(uint32_t),
                              m_GpuSceneGaussianSplatPointDrawIdScratch.data());
                }
            }

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseBack;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewBack;
        }
        else
        {
            // Reuse previous snapshot when neither cooked world nor resource pool changed.
            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }

        m_GpuSceneDirtyTracker.markBuilt(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);

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
            if (imguiService && !cam.isXRView)
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
        const auto  assetMemoryStats = assetService.memoryStats();
        const auto memoryStats = rd.getMemoryStats();
        m_RuntimeProfiler.setMemoryStats(assetMemoryStats.cpuCacheBytes,
                         memoryStats.cpuCacheBytes,
                         memoryStats.gpuDeviceLocalBytes,
                         memoryStats.gpuHostVisibleBytes);
        m_RuntimeProfiler.setGpuFrameMs(rd.consumeGpuFrameMs());
        const auto renderFrameCpuEnd = std::chrono::steady_clock::now();
        m_RuntimeProfiler.setCpuRenderMs(
            std::chrono::duration<double, std::milli>(renderFrameCpuEnd - renderFrameCpuStart).count());
        m_RuntimeProfiler.endFrame();
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
