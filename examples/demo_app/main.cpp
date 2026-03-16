#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <iostream>

using namespace vultra;

class DemoApp final : public DemoAppHost
{
protected:
    void onPostConfigureDemo(Engine& engine) override
    {
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();

        auto& world = worldService.world();
        auto  root  = sceneService.instantiateScene(world, "res://scenes/test.vscn");

        VULTRA_CLIENT_INFO("Loaded world from scene: \"res://scenes/test.vscn\"");

        auto& registry = world.registry();
        registry.view<NameComponent, TransformComponent>().each(
            [](auto, NameComponent& name, TransformComponent& transform) {
                std::cout << "Entity with transform: " << name.name << "\n";

                std::cout << " Position: " << transform.position.x << ", " << transform.position.y << ", "
                          << transform.position.z << "\n";
                std::cout << " Rotation: " << transform.rotation.x << ", " << transform.rotation.y << ", "
                          << transform.rotation.z << ", " << transform.rotation.w << "\n";
                std::cout << " Scale:    " << transform.scale.x << ", " << transform.scale.y << ", "
                          << transform.scale.z << "\n";
            });

        registry.view<NameComponent, MeshComponent>().each([](auto, NameComponent& name, MeshComponent& mesh) {
            std::cout << "Entity with mesh: " << name.name << "\n";
            std::cout << " Mesh UUID: " << mesh.mesh.toString() << "\n";
        });

        sceneService.saveWorldAsSceneSync("res://scenes/test_saved.vscn", world);
    }
};

int main()
{
    DemoApp app {};
    return app.run();
}
