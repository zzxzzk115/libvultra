#include "vultra/function/rendering/render_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/imgui_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

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
            renderer->init(services);
        }

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

        m_GpuSceneBack.clear();
        m_GpuSceneFront.clear();

        for (auto& [key, renderer] : m_Renderers)
        {
            renderer = nullptr;
        }
        m_Renderers.clear();
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
        // Required services
        auto& backendService     = ctx().services.require<IRenderBackendService>();
        auto& worldService       = ctx().services.require<IWorldService>();
        auto& camService         = ctx().services.require<ICameraService>();
        auto& gpuResourceService = ctx().services.require<IGpuResourceService>();
        auto& assetService       = ctx().services.require<IAssetService>();

        // Optional ImGui service for rendering ImGui on top of frame.
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        // Optional frame debugger service for GPU capture.
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();

<<<<<<< HEAD
        auto& rd = backendService.renderDevice();

=======
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88
        World& world = worldService.world();

        // Asset upload/update stage (main thread)
        assetService.update(m_FrameCounter);

        // Cook render instances
        RenderWorldCooker cooker {};
        cooker.cook(world, assetService, m_RenderWorldBack);

        // Cook render cameras
        m_RenderWorldBack.frameIndex = m_FrameCounter;
        m_RenderWorldBack.cameras    = camService.cameras();

        // Build per-frame GPU-driven scene tables (draws + indirect commands).
        // These tables are consumed by GPU-driven passes (gl_DrawID indexed).
        {
            // Bind global GPU resource pool
            m_GpuSceneBack.resources = &gpuResourceService.pool();

            const auto& pool = *m_GpuSceneBack.resources;

            m_GpuSceneBack.beginFrame(pool);

            // Build one draw per render instance (no instancing yet).
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                if (inst.meshIndex >= pool.meshes.size())
                    continue;

                const auto& mesh = pool.meshes[inst.meshIndex];

                resource::GpuDrawRecord dr;
                dr.vertexAddress = mesh.vertexBufferAddress;
                dr.indexAddress  = pool.geometry.index32Address;
                dr.model         = inst.worldMatrix;
                dr.materialIndex = inst.materialIndex;
                dr.firstIndex    = mesh.indexBase;
                dr.indexCount    = mesh.indexCount;
                dr.flags         = 0;
                dr.padding0      = 0;

                m_GpuSceneBack.pushDraw(dr);
            }

            // Upload draw table.
            m_GpuSceneBack.uploadTables(rd);

            // Build and upload indirect commands (indexed; binds global geometry index buffer).
            m_GpuSceneBack.buildIndirectIndexedFromDraws();
            m_GpuSceneBack.uploadIndirect(rd);

            m_RenderWorldBack.gpuScene = &m_GpuSceneBack;
        }

        std::swap(m_RenderWorldFront, m_RenderWorldBack);
        std::swap(m_GpuSceneFront, m_GpuSceneBack);
        m_RenderWorldFront.gpuScene = &m_GpuSceneFront;
        m_RenderWorldBack.gpuScene  = &m_GpuSceneBack;

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
            return;

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

            cam.ensureUniformBuffer(rd);

            FrameGraph           fg {};
            FrameGraphBlackboard bb {};

            rhi::Texture* target = cam.target ? cam.target : &defaultTarget;
            if (!target)
                continue;

            RenderContext rc {
                .cb = cb,
                .rd = backendService.renderDevice(),
                .framebufferInfo =
                    rhi::FramebufferInfo {
                        .area             = {.extent = target->getExtent()},
                        .colorAttachments = {rhi::AttachmentInfo {.target = target, .clearValue = cam.clearValue}},
                    },
                .fg          = fg,
                .bb          = bb,
                .renderWorld = m_RenderWorldFront,
                .camera      = cam,
                .dt          = dt,
            };

            renderer->render(rc);

            // Compile and execute the frame graph
            fg.compile();
            fg.execute(&rc, m_TransientResources.get());

            // Optional ImGui rendering per camera
            if (imguiService)
            {
                imguiService->render(cb, *rc.framebufferInfo);
            }
        }

        backendService.endFrame();
<<<<<<< HEAD
=======

        // Capture end
        if (frameDebuggerService)
        {
            frameDebuggerService->captureEnd();
        }
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88
    }

    void RenderSystem::onPreRender()
    {
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        if (imguiService)
        {
            imguiService->begin();
            for (auto& [key, renderer] : m_Renderers)
            {
                if (renderer)
                    renderer->onImGui();
            }
            imguiService->end();
        }
    }

    void RenderSystem::onRender() { renderFrame(); }

    void RenderSystem::onPostRender()
    {
<<<<<<< HEAD
        auto& backendService       = ctx().services.require<IRenderBackendService>();
        auto* imguiService         = ctx().services.tryGet<IImGuiService>();
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();
=======
        auto& backendService = ctx().services.require<IRenderBackendService>();
        auto* imguiService   = ctx().services.tryGet<IImGuiService>();
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88

        if (imguiService)
        {
            imguiService->postRender();
        }

        backendService.present();
<<<<<<< HEAD

        // Capture end
        if (frameDebuggerService)
        {
            frameDebuggerService->captureEnd();
        }
=======
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88
    }
} // namespace vultra
