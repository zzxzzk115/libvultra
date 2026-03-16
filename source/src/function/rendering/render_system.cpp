#include "vultra/function/rendering/render_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
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

namespace vultra
{
    void RenderWorldCooker::cook(World& world, IAssetService& assets, RenderWorld& out)
    {
        out.clear();

        auto& reg = world.registry();

        auto view = reg.view<IDComponent, TransformComponent, MeshComponent>();
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
        auto& backendService     = ctx().services.require<IRenderBackendService>();
        auto& worldService       = ctx().services.require<IWorldService>();
        auto& camService         = ctx().services.require<ICameraService>();
        auto& gpuResourceService = ctx().services.require<IGpuResourceService>();
        auto& assetService       = ctx().services.require<IAssetService>();
        auto& shaderService      = ctx().services.require<IShaderService>();

        // Optional ImGui service for rendering ImGui on top of frame.
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        // Optional frame debugger service for GPU capture.
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();

        auto& rd = backendService.renderDevice();

        World& world = worldService.world();

        // Asset upload/update stage (main thread)
        assetService.update(m_FrameCounter);

        // Cook render instances
        RenderWorldCooker cooker {};
        cooker.cook(world, assetService, m_RenderWorldBack);

        // Cook render cameras
        m_RenderWorldBack.frameIndex = m_FrameCounter;
        m_RenderWorldBack.cameras    = camService.cameras();

        // Build GPU scene database + per-view draw state.
        //
        // Database layer:
        // - stable pointer to global resource pool
        // - scene/instance tables
        //
        // View layer:
        // - draw table
        // - indirect commands
        {
            const auto& pool = gpuResourceService.pool();

            m_GpuSceneDatabaseBack.beginFrame(pool);
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
            m_GpuSceneDatabaseBack.uploadSceneTables(rd);

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
                m_GpuSceneViewBack.prepareGpuDrivenBuffers(rd, maxMeshletDraws, maxMeshletDraws);
            }
            else
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eCpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(maxMeshletDraws, maxMeshletDraws);
                m_GpuSceneViewBack.ensureVisibleMeshletBuffers(rd);

                std::vector<resource::GpuDrawRecord> stagedDraws;
                for (uint32_t instanceIndex = 0;
                     instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                     ++instanceIndex)
                {
                    const auto& inst = m_RenderWorldBack.instances[instanceIndex];
                    if (inst.meshIndex >= pool.meshes.size())
                        continue;
                    if (instanceIndex >= m_GpuSceneDatabaseBack.instances.size())
                        continue;

                    const auto& gpuInst = m_GpuSceneDatabaseBack.instances[instanceIndex];
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
                        stagedDraws.push_back(dr);
                    }
                }

                std::stable_sort(stagedDraws.begin(), stagedDraws.end(), [](const auto& a, const auto& b) {
                    if (a.materialIndex != b.materialIndex)
                        return a.materialIndex < b.materialIndex;
                    return a.primitiveIndex < b.primitiveIndex;
                });

                for (const auto& dr : stagedDraws)
                    m_GpuSceneViewBack.pushMeshletDraw(dr);

                m_GpuSceneViewBack.uploadDraws(rd);
                m_GpuSceneViewBack.buildIndirectFromDraws(pool);
                m_GpuSceneViewBack.uploadIndirect(rd);
            }

            // Stage gaussian splat draws (GPU-driven: cull shader writes the indirect buffer).
            {
                const uint32_t maxSplatDraws =
                    static_cast<uint32_t>(m_RenderWorldBack.splatInstances.size());

                static bool s_LoggedGaussianSplatStage = false;

                m_GpuSceneViewBack.setGaussianSplatGpuDrivenCaps(maxSplatDraws);
                m_GpuSceneViewBack.ensureGaussianSplatDrawBuffer(rd);

                for (const auto& inst : m_RenderWorldBack.splatInstances)
                {
                    if (inst.splatIndex >= pool.gaussianSplats.size())
                        continue;

                    resource::GpuDrawRecord dr;
                    dr.primitiveIndex = inst.splatIndex;
                    dr.materialIndex = 0;
                    dr.flags         = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eGaussianSplat);
                    dr.model         = inst.worldMatrix;
                    dr.padding0      = pool.gaussianSplats[inst.splatIndex].pointCount;
                    m_GpuSceneViewBack.pushGaussianSplatDraw(dr);

                    if (!s_LoggedGaussianSplatStage)
                    {
                        VULTRA_CORE_INFO("[GaussianSplat] stage draw splatIndex={} pointCount={} totalPoolSplats={}",
                                         inst.splatIndex,
                                         pool.gaussianSplats[inst.splatIndex].pointCount,
                                         pool.gaussianSplats.size());
                    }
                }

                if (!s_LoggedGaussianSplatStage)
                {
                    VULTRA_CORE_INFO("[GaussianSplat] staged instances={} gpuDraws={} maxDraws={}",
                                     m_RenderWorldBack.splatInstances.size(),
                                     m_GpuSceneViewBack.gaussianSplatDraws.size(),
                                     maxSplatDraws);
                    s_LoggedGaussianSplatStage = true;
                }

                m_GpuSceneViewBack.uploadGaussianSplatDraws(rd);
            }

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseBack;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewBack;
        }

        std::swap(m_RenderWorldFront, m_RenderWorldBack);
        std::swap(m_GpuSceneDatabaseFront, m_GpuSceneDatabaseBack);
        std::swap(m_GpuSceneViewFront, m_GpuSceneViewBack);
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

        auto cams = m_RenderWorldFront.cameras;
        if (cams.empty())
            return;

        std::stable_sort(cams.begin(), cams.end(), [](const RenderCamera& a, const RenderCamera& b) {
            return a.priority < b.priority;
        });

        // Capture start
        if (frameDebuggerService)
        {
            frameDebuggerService->captureStart();
        }

        // Begin frame once (desktop backbuffer case).
        // XR backend later can override policy (e.g., beginFrame per XR frame).
        if (!backendService.beginFrame())
        {
            m_SkipRender = true;
            return;
        }

        auto& cb = backendService.commandBuffer();

        // Default target for cameras without explicit RT
        auto& defaultTarget = backendService.backbuffer();

        // TODO: TimeSystem, for now use 0
        const fsec dt {0};

        for (auto& cam : cams)
        {
            auto renderer = resolveRenderer(cam);
            if (!renderer)
                continue;

            FrameGraph             fg {};
            FrameGraphBlackboard   bb {};
            FrameGraphDataRegistry dataRegistry {};

            rhi::Texture* target = cam.target ? cam.target : &defaultTarget;
            if (!target)
                continue;

            RenderView view {
                .renderWorld      = &m_RenderWorldFront,
                .camera           = &cam,
                .target           = target,
                .extent           = target->getExtent(),
                .clearValue       = cam.clearValue,
                .gpuSceneDatabase = m_RenderWorldFront.gpuSceneDatabase,
                .gpuSceneView     = m_RenderWorldFront.gpuSceneView,
            };

            rhi::FramebufferInfo fbInfo {
                .area             = {.extent = target->getExtent()},
                .colorAttachments = {rhi::AttachmentInfo {.target = target, .clearValue = cam.clearValue}},
            };

            ViewRenderData viewData {
                .view            = view,
                .framebufferInfo = fbInfo,
            };

            {
                ImmediateResourceUploader immediateUploader {m_FrameResources, rd};
                prepareCameraData(immediateUploader, viewData, target->getExtent(), cam);
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

            {
                FrameGraphResourceUploader fgUploader {fg};
                prepareFrameData(fgUploader, m_PreparedFrameData, m_RenderWorldFront.frameIndex, 0.0f, 0.0f);
                prepareCameraData(fgUploader, viewData, target->getExtent(), cam);
                bb.add<FrameData>(m_PreparedFrameData.frameData);
                bb.add<CameraData>(viewData.cameraData);
            }

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
                std::ofstream ofs(vbase::executable_dir() / "framegraph.dot");
                ofs << fg;
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

            fg.execute(&frameGraphExecCtx, m_TransientResources.get());

            // Optional ImGui rendering per camera
            if (imguiService)
            {
                imguiService->begin();
                renderer->onImGui();
                imguiService->end();

                rhi::prepareForAttachment(cb, *target, false);
                imguiService->render(cb, fbInfo);
            }
        }

        m_TransientResources->update();
        backendService.endFrame();

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
