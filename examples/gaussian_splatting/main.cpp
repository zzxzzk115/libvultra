#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

using namespace vultra;

class GaussianSplattingDemoApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Gaussian Splatting Demo"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& sceneService   = engine.ctx().services.require<ISceneService>();
        auto& worldService   = engine.ctx().services.require<IWorldService>();
        auto& renderService  = engine.ctx().services.require<IRenderService>();
        auto& backendService = engine.ctx().services.require<IRenderBackendService>();

        if (backendService.renderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            renderService.gaussianSplatSettings().baselineMode = GaussianSplatBaselineMode::eOrderedClod;
            VULTRA_CLIENT_INFO(
                "WebGPU gaussian renderer uses ordered-clod to stay within compute storage-buffer limits");
        }

        auto& world = worldService.world();
        sceneService.instantiateScene(world, "res://scenes/3dgs_example.vmanifest");

        VULTRA_CLIENT_INFO("Loaded world from scene manifest: \"res://scenes/3dgs_example.vmanifest\"");
    }
};

int main(int argc, char** argv)
{
    GaussianSplattingDemoApp app {};
    return app.run(argc, argv);
}
