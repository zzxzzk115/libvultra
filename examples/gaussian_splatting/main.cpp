#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/services/asset_service.hpp>
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

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();

        auto& world = worldService.world();
        sceneService.instantiateScene(world, "res://scenes/3dgs_example.vmanifest");

        VULTRA_CLIENT_INFO("Loaded world from scene manifest: \"res://scenes/3dgs_example.vmanifest\"");
    }
};

int main()
{
    GaussianSplattingDemoApp app {};
    return app.run();
}
