#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/structs/render_device_structs.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include "../example_renderer.hpp"

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

using namespace vultra;

namespace
{
    constexpr const char* kModelUri = "res://models/Sponza/Sponza.gltf";

    void addNamedTransform(entt::registry& reg, entt::entity entity, const char* name, const TransformComponent& transform)
    {
        reg.emplace<NameComponent>(entity, NameComponent {name});
        reg.emplace<TransformComponent>(entity, transform);
    }
} // namespace

class SponzaApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Sponza"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eRayTracing | rhi::RenderDeviceFeatureFlagBits::eMeshShader;
    }

    Ref<Renderer> makeRenderer() const override
    {
        return createRef<examples::ExampleUniversalRenderer>(
            "Sponza Example",
            [](Services services) {
                ImGui::TextUnformatted("This example renders the Sponza scene with the SRP renderer.");
                examples::drawNamedLightControls(services, "Warm Point Light");
                examples::drawExampleRenderSettings(services);
            });
    }

    FPSCameraController makeFPSCameraController() const override
    {
        auto controller          = DemoAppHost::makeFPSCameraController();
        controller.position      = {8.0f, 1.5f, -0.5f};
        controller.yawDegrees    = 180.0f;
        controller.pitchDegrees  = 0.0f;
        controller.orbitDistance = 8.0f;
        controller.moveSpeed     = 6.0f;
        controller.zFar          = 500.0f;
        return controller;
    }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& assetService = engine.ctx().services.require<IAssetService>();
        auto& renderService = engine.ctx().services.require<IRenderService>();
        auto& world = engine.ctx().services.require<IWorldService>().world();
        auto& reg   = world.registry();

        auto mesh = assetService.loadMeshSync(kModelUri);
        if (!mesh)
        {
            VULTRA_CLIENT_ERROR("[Sponza] Failed to load mesh: {}", kModelUri);
            return;
        }

        auto model = world.createEntity();
        addNamedTransform(reg,
                          model,
                          "Sponza",
                          TransformComponent {
                              .position = {0.0f, 0.0f, 0.0f},
                              .rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f},
                              .scale    = {1.0f, 1.0f, 1.0f},
                          });
        reg.emplace<MeshComponent>(model, MeshComponent {.mesh = mesh.uuid()});

        auto pointLight = world.createEntity();
        addNamedTransform(reg,
                          pointLight,
                          "Warm Point Light",
                          TransformComponent {
                              .position = {-8.0f, 2.0f, -0.5f},
                              .rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f},
                              .scale    = {1.0f, 1.0f, 1.0f},
                          });
        reg.emplace<LightComponent>(pointLight,
                                    LightComponent {
                                        .kind      = 1u,
                                        .color     = {0.9f, 0.85f, 0.45f},
                                        .intensity = 50.0f,
                                        .range     = 5.0f,
                                    });

        auto& settings = renderService.builtinRenderSettings();
        settings.pbrLighting.enableIBL        = false;
        settings.pbrLighting.ambientIntensity = 0.35f;
        settings.shadow.coverageRadius        = 35.0f;
        settings.shadow.lightDistance         = 80.0f;
        settings.shadow.zRange                = 80.0f;
    }
};

VULTRA_DEMO_APP_MAIN(SponzaApp)
