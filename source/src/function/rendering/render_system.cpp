#include "vultra/function/rendering/render_system.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/function/rendering/render_camera.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/world_service.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <algorithm>

namespace vultra
{
    bool RenderSystem::onInit()
    {
        ctx().services.provide<IRenderService>(this);

        auto& backendService = ctx().services.require<IRenderBackendService>();
        m_TransientResources = createScope<framegraph::TransientResources>(backendService.renderDevice());

        for (auto& [key, renderer] : m_Renderers)
        {
            renderer->init(backendService);
        }

        return true;
    }

    void RenderSystem::onShutdown()
    {
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

        if (!worldService || !camService)
            return;

        World& world = worldService->world();

        auto cams = camService->cameras();
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
                .fg     = fg,
                .bb     = bb,
                .world  = world,
                .camera = cam,
                .dt     = dt,
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
