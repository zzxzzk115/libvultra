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

#include "../../example_renderer.hpp"

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

using namespace vultra;

namespace
{
    constexpr const char* kSponzaUri = "res://models/Sponza/Sponza.gltf";

    void addNamedTransform(entt::registry& reg, entt::entity entity, const char* name, const TransformComponent& transform)
    {
        reg.emplace<NameComponent>(entity, NameComponent {name});
        reg.emplace<TransformComponent>(entity, transform);
    }
} // namespace

class OpenXRSponzaExampleApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "OpenXR Sponza"; }

    rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const override
    {
        return rhi::RenderDeviceFeatureFlagBits::eXR;
    }

    Ref<Renderer> makeRenderer() const override
    {
        return createRef<examples::ExampleUniversalRenderer>(
            "OpenXR Sponza Example",
            [](Services services) {
                ImGui::TextUnformatted("This example renders Sponza through the OpenXR backend.");
                examples::drawNamedLightControls(services, "XR Key Light");
                examples::drawExampleRenderSettings(services);
            },
            true);
    }

    FPSCameraController makeFPSCameraController() const override
    {
        auto controller          = DemoAppHost::makeFPSCameraController();
        controller.position      = {8.0f, 1.5f, -0.5f};
        controller.yawDegrees    = 180.0f;
        controller.pitchDegrees  = 0.0f;
        controller.orbitDistance = 8.0f;
        controller.zFar          = 500.0f;
        return controller;
    }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& assetService = engine.ctx().services.require<IAssetService>();
        auto& renderService = engine.ctx().services.require<IRenderService>();
        auto& world = engine.ctx().services.require<IWorldService>().world();
        auto& reg   = world.registry();

        auto mesh = assetService.loadMeshSync(kSponzaUri);
        if (!mesh)
        {
            VULTRA_CLIENT_ERROR("[OpenXRSponza] Failed to load mesh: {}", kSponzaUri);
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

        auto light = world.createEntity();
        addNamedTransform(reg,
                          light,
                          "XR Key Light",
                          TransformComponent {
                              .position = {-8.0f, 2.0f, -0.5f},
                              .rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f},
                              .scale    = {1.0f, 1.0f, 1.0f},
                          });
        reg.emplace<LightComponent>(light,
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

        VULTRA_CLIENT_INFO("Loaded OpenXR Sponza scene content");
    }
};

VULTRA_DEMO_APP_MAIN(OpenXRSponzaExampleApp)
