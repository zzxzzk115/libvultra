#include <vultra/core/engine/engine.hpp>
#include <vultra/core/timing/timing_system.hpp>
#include <vultra/function/jobs/job_system.hpp>
#include <vultra/function/physics/physics_system.hpp>
#include <vultra/function/services/physics_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world_system.hpp>

#include <cstdio>

int main()
{
    vultra::Engine engine;
    engine.emplaceSubsystem<vultra::TimingSystem>();
    engine.emplaceSubsystem<vultra::JobSystem>();
    engine.emplaceSubsystem<vultra::WorldSystem>();
    engine.emplaceSubsystem<vultra::PhysicsSystem>();

    if (!engine.initCore())
    {
        std::fprintf(stderr, "engine init failed\n");
        return 1;
    }

    auto& world = engine.ctx().services.require<vultra::IWorldService>().world();
    auto& reg   = world.registry();

    auto floor = world.createEntity();
    auto& floorTransform = reg.get_or_emplace<vultra::TransformComponent>(floor);
    floorTransform.position = {0.0f, -0.5f, 0.0f};
    floorTransform.scale    = {10.0f, 1.0f, 10.0f};
    floorTransform.dirty    = true;
    reg.emplace<vultra::RigidBodyComponent>(floor,
                                            vultra::RigidBodyComponent {
                                                .motionType = 0u,
                                                .objectLayer = 0u,
                                            });
    reg.emplace<vultra::BoxShapeComponent>(floor, vultra::BoxShapeComponent {.halfExtents = {5.0f, 0.5f, 5.0f}});

    auto sphere = world.createEntity();
    auto& sphereTransform = reg.get_or_emplace<vultra::TransformComponent>(sphere);
    sphereTransform.position = {0.0f, 4.0f, 0.0f};
    sphereTransform.dirty    = true;
    reg.emplace<vultra::RigidBodyComponent>(sphere);
    reg.emplace<vultra::SphereShapeComponent>(sphere, vultra::SphereShapeComponent {.radius = 0.5f});

    auto capsule = world.createEntity();
    auto& capsuleTransform = reg.get_or_emplace<vultra::TransformComponent>(capsule);
    capsuleTransform.position = {2.0f, 3.0f, 0.0f};
    capsuleTransform.dirty    = true;
    reg.emplace<vultra::RigidBodyComponent>(capsule);
    reg.emplace<vultra::CapsuleShapeComponent>(capsule);

    auto& physics = engine.ctx().services.require<vultra::IPhysicsService>();
    const float initialY = sphereTransform.position.y;

    physics.setPlaybackState(false, false);
    if (physics.bodyCount() != 0u)
    {
        std::fprintf(stderr, "physics bodies survived stop: %u\n", physics.bodyCount());
        engine.shutdownCore();
        return 2;
    }
    for (int i = 0; i < 30; ++i)
        engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    const float stoppedY = sphereTransform.position.y;
    if (stoppedY != initialY)
    {
        std::fprintf(stderr, "sphere moved while stopped: start=%f stopped=%f\n", initialY, stoppedY);
        engine.shutdownCore();
        return 3;
    }

    physics.setPlaybackState(true, true);
    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    if (physics.bodyCount() != 3u)
    {
        std::fprintf(stderr, "physics bodies were not recreated while paused: %u\n", physics.bodyCount());
        engine.shutdownCore();
        return 4;
    }
    for (int i = 0; i < 30; ++i)
        engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    const float pausedY = sphereTransform.position.y;
    if (pausedY != initialY)
    {
        std::fprintf(stderr, "sphere moved while paused: start=%f paused=%f\n", initialY, pausedY);
        engine.shutdownCore();
        return 5;
    }

    physics.requestSingleStep();
    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    const float steppedY = reg.get<vultra::TransformComponent>(sphere).position.y;
    if (!(steppedY < initialY))
    {
        std::fprintf(stderr, "sphere did not move on single step: start=%f stepped=%f\n", initialY, steppedY);
        engine.shutdownCore();
        return 6;
    }

    physics.setEnabled(false);
    if (physics.bodyCount() != 0u)
    {
        std::fprintf(stderr, "physics bodies survived disable: %u\n", physics.bodyCount());
        engine.shutdownCore();
        return 7;
    }
    physics.setEnabled(true);

    physics.setPlaybackState(true, false);
    const float catchupStartY = reg.get<vultra::TransformComponent>(sphere).position.y;
    engine.tickFrame(vultra::fsec {0.1f});
    const float catchupEndY = reg.get<vultra::TransformComponent>(sphere).position.y;
    if (!((catchupStartY - catchupEndY) > 0.05f))
    {
        std::fprintf(stderr, "physics did not catch up on low render fps: start=%f end=%f\n", catchupStartY, catchupEndY);
        engine.shutdownCore();
        return 8;
    }

    const float runStartY = sphereTransform.position.y;
    for (int i = 0; i < 180; ++i)
        engine.tickFrame(vultra::fsec {1.0f / 60.0f});

    const auto endY = reg.get<vultra::TransformComponent>(sphere).position.y;
    if (!(endY < runStartY))
    {
        std::fprintf(stderr, "sphere did not fall: start=%f end=%f\n", runStartY, endY);
        engine.shutdownCore();
        return 9;
    }
    if (!(endY > 0.35f))
    {
        std::fprintf(stderr, "sphere fell through floor: end=%f\n", endY);
        engine.shutdownCore();
        return 10;
    }

    if (physics.bodyCount() != 3u)
    {
        std::fprintf(stderr, "unexpected body count: %u\n", physics.bodyCount());
        engine.shutdownCore();
        return 11;
    }

    engine.shutdownCore();
    return 0;
}
