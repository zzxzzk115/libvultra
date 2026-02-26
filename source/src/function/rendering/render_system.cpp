#include "vultra/function/rendering/render_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
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
        
        VULTRA_CORE_TRACE("[RenderSystem] Creating transient resources");
        m_TransientResources = createScope<framegraph::TransientResources>(backendService.renderDevice());

        VULTRA_CORE_TRACE("[RenderSystem] Initializing renderers");
        for (auto& [key, renderer] : m_Renderers)
        {
            VULTRA_CORE_TRACE("[RenderSystem]     Initializing renderer: {}", key);
            renderer->init(backendService);
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

        for (auto& [key, renderer] : m_Renderers)
        {
            renderer.reset();
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

    void RenderSystem::renderFrame()
    {
        auto& backendService = ctx().services.require<IRenderBackendService>();
        auto* worldService   = ctx().services.tryGet<IWorldService>();
        auto* camService     = ctx().services.tryGet<ICameraService>();
        auto* assetService   = ctx().services.tryGet<IAssetService>();

        if (!worldService || !camService || !assetService)
            return;

        World& world = worldService->world();

        // Asset upload/update stage (main thread)
        assetService->update(m_FrameCounter);

        // Cook render instances
        RenderWorldCooker cooker {};
        cooker.cook(world, *assetService, m_RenderWorldBack);

        // Cook render cameras
        m_RenderWorldBack.frameIndex = m_FrameCounter;
        m_RenderWorldBack.cameras    = camService->cameras();

        // Bind global GPU resource pool
        m_RenderWorldBack.gpuResources = &assetService->gpuResourcePool();

        std::swap(m_RenderWorldFront, m_RenderWorldBack);

        ++m_FrameCounter;

        auto cams = m_RenderWorldFront.cameras;
        if (cams.empty())
            return;

        std::stable_sort(cams.begin(), cams.end(), [](const RenderCamera& a, const RenderCamera& b) {
            return a.priority < b.priority;
        });

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
                        .colorAttachments = {rhi::AttachmentInfo {.target = target}},
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
        }

        backendService.endFrame();
        backendService.present();
    }

    void RenderSystem::onRender() { renderFrame(); }
} // namespace vultra
