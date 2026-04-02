#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

using namespace vultra;

class OpenXRGaussianSplattingApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "OpenXR Gaussian Splatting"; }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();

        auto& world = worldService.world();
        sceneService.instantiateScene(world, "res://scenes/3dgs_example.vmanifest");

        VULTRA_CLIENT_INFO("Loaded world from scene manifest: \"res://scenes/3dgs_example.vmanifest\"");
    }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eXR;
    }
};

int main()
{
    OpenXRGaussianSplattingApp app {};
    return app.run();
}
