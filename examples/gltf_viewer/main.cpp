#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/gpu_resource_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
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
    constexpr const char* kModelUri = "res://models/DamagedHelmet/DamagedHelmet.gltf";
    constexpr const char* kEnvironmentMapUri = "res://textures/environment_maps/citrus_orchard_puresky_1k.hdr";

    void addNamedTransform(entt::registry& reg, entt::entity entity, const char* name, const TransformComponent& transform)
    {
        reg.emplace<NameComponent>(entity, NameComponent {name});
        reg.emplace<TransformComponent>(entity, transform);
    }
} // namespace

class GLTFViewerApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "GLTF Viewer"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    Ref<Renderer> makeRenderer() const override
    {
        return createRef<examples::ExampleUniversalRenderer>(
            "GLTF Viewer",
            [](Services services) {
                ImGui::TextUnformatted("This example renders the Damaged Helmet glTF asset.");
                examples::drawNamedLightControls(services, "Key Light");
                examples::drawExampleRenderSettings(services);
            });
    }

    FPSCameraController makeFPSCameraController() const override
    {
        auto controller          = DemoAppHost::makeFPSCameraController();
        controller.position      = {0.0f, 0.8f, 4.0f};
        controller.yawDegrees    = -90.0f;
        controller.pitchDegrees  = -8.0f;
        controller.orbitDistance = 4.0f;
        controller.zFar          = 100.0f;
        return controller;
    }

    void onPostConfigureDemo(Engine& engine) override
    {
        auto& assetService = engine.ctx().services.require<IAssetService>();
        auto& gpuResources = engine.ctx().services.require<IGpuResourceService>();
        auto& renderService = engine.ctx().services.require<IRenderService>();
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& world = engine.ctx().services.require<IWorldService>().world();
        auto& reg   = world.registry();

        auto model = sceneService.instantiateScene(world, kModelUri);
        if (model == entt::null)
        {
            VULTRA_CLIENT_ERROR("[GLTFViewer] Failed to instantiate scene: {}", kModelUri);
            return;
        }
        if (auto* name = reg.try_get<NameComponent>(model))
            name->name = "Damaged Helmet";

        m_EnvironmentMap = assetService.loadTextureSync(kEnvironmentMapUri);
        if (!m_EnvironmentMap)
        {
            VULTRA_CLIENT_ERROR("[GLTFViewer] Failed to load environment map: {}", kEnvironmentMapUri);
        }

        auto sun = world.createEntity();
        addNamedTransform(reg,
                          sun,
                          "Key Light",
                          TransformComponent {
                              .position = {0.0f, 3.0f, 3.0f},
                              .rotation = glm::quat {-0.702802f, 0.0788313f, -0.0751287f, 0.703001f},
                              .scale    = {1.0f, 1.0f, 1.0f},
                          });
        reg.emplace<LightComponent>(sun,
                                    LightComponent {
                                        .kind        = 0u,
                                        .color       = {1.0f, 0.96f, 0.9f},
                                        .intensity   = 4.0f,
                                        .range       = 100.0f,
                                        .castsShadow = true,
                                    });

        auto& settings = renderService.builtinRenderSettings();
        settings.pbrLighting.enableIBL        = true;
        settings.pbrLighting.iblColor         = {1.0f, 1.0f, 1.0f};
        settings.pbrLighting.iblIntensity     = 1.0f;
        settings.pbrLighting.ambientIntensity = 0.2f;
        settings.pbrLighting.showSkybox       = m_EnvironmentMap.valid();
        if (m_EnvironmentMap.valid() && m_EnvironmentMap.gpuIndex() < gpuResources.pool().textures.size())
        {
            settings.pbrLighting.environmentMap =
                gpuResources.pool().textures[m_EnvironmentMap.gpuIndex()].texture.get();
        }
        settings.ssr.enabled          = true;
        settings.ssr.reflectionFactor = 0.6f;
        settings.ssr.maxSteps         = 48;
        settings.ssr.binaryRefinement = 5;
        settings.ssr.stride           = 0.12f;
        settings.ssr.thickness        = 0.35f;
        settings.ssao.enabled         = true;
        settings.ssao.radius          = 1.2f;
        settings.ssao.bias            = 0.04f;
        settings.ssao.intensity       = 1.2f;
        settings.ssao.stepCount       = 4;
        settings.ssao.directionCount  = 8;
        settings.shadow.coverageRadius = 20.0f;
    }

private:
    AssetHandle<vasset::VTexture, resource::GpuTexture> m_EnvironmentMap;
};

VULTRA_DEMO_APP_MAIN(GLTFViewerApp)
