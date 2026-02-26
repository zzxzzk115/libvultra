#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"
#include "vultra/function/resource/gpu_scene.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/world_service.hpp"

#include <string>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    // RenderSystem (SRP host):
    // - Reads cooked cameras from CameraSystem
    // - Resolves renderer per camera.rendererKey
    // - Builds FrameGraph per camera using RenderContext
    // - Compiles & executes
    class RenderSystem final : public EngineSubsystem, public IRenderService
    {
    public:
        ENGINE_SUBSYSTEM(RenderSystem)

        bool onInit() override;
        void onShutdown() override;

        void onRender() override;

        // IRenderService
        void registerRenderer(Ref<Renderer> renderer) override;
        void renderFrame() override;

        // Optional: set default renderer key used if camera.rendererKey not found
        void setDefaultRendererKey(std::string key) { m_DefaultRendererKey = std::move(key); }

        // Optional: set fallback backbuffer target (can be used for offline rendering)
        void setBackbufferTarget(rhi::Texture* tex) { m_Backbuffer = tex; }

        // Cooked render world (read-only for renderer)
        const RenderWorld& renderWorld() const { return m_RenderWorldFront; }

    private:
        Ref<Renderer> resolveRenderer(const RenderCamera& cam) const;

    private:
        std::unordered_map<std::string, Ref<Renderer>> m_Renderers;
        std::string                                    m_DefaultRendererKey {"builtin"};

        rhi::Texture*                                   m_Backbuffer {nullptr};
        std::unique_ptr<framegraph::TransientResources> m_TransientResources {nullptr};

        RenderWorld m_RenderWorldFront {};
        RenderWorld m_RenderWorldBack {};

        resource::GpuScene m_GpuSceneFront {};
        resource::GpuScene m_GpuSceneBack {};

        uint64_t m_FrameCounter {0};
    };

    // Cook World into RenderWorld.
    class RenderWorldCooker
    {
    public:
        static void cook(World& world, IAssetService& assets, RenderWorld& out);
    };
} // namespace vultra
