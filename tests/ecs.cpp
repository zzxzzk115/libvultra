#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/ext/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Callback>
    void expectError(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Invalid ECS operation was accepted");
    }

    struct Velocity
    {
        float x;
    };

    class StopOnEnter final : public Node
    {
    public:
        StopOnEnter() :
            Node("StopOnEnter")
        {
        }

        void onEnterTree() override
        {
            tree()->stop();
        }
    };

    class StopOnExit final : public Node
    {
    public:
        StopOnExit() :
            Node("StopOnExit")
        {
        }

        void onExitTree() override
        {
            tree()->stop();
        }
    };

    void lifecycleCancellation()
    {
        SceneTree  world(std::make_unique<StopOnEnter>());
        bool       called = false;
        const auto system = world.addSystem(ScenePhase::ePhysics,
                                            [&called](SceneTree&, double)
                                            {
                                                called = true;
                                            });
        world.physicsStep();
        world.advance(1);
        require(!called && !world.running() && world.physicsFrame() == 0 && !world.root().isReady(),
                "Stopping in EnterTree still ran simulation or advanced the fixed clock");
        world.removeSystem(system);
        SceneTree live(std::make_unique<Node>("Live"));
        live.start();
        live.addChild(live.root(), std::make_unique<StopOnEnter>());
        require(!live.running(), "Immediate child attachment left a lifecycle stop request pending");
        auto& stopping    = live.addChild(live.root(), std::make_unique<StopOnExit>());
        auto& destination = live.addChild(live.root(), std::make_unique<Node>("Destination"));
        // Retire the always-stopping entry node before restarting the remaining scene.
        live.remove(*live.root().children().front());
        live.start();
        live.reparent(stopping, destination);
        require(!live.running(), "Immediate reparent left an ExitTree stop request pending");
        live.start();
        auto detached = live.remove(stopping);
        require(!live.running() && !detached->tree(), "Immediate removal did not honor ExitTree stop at its boundary");
    }

    void deferredOrdering()
    {
        SceneTree world(std::make_unique<Node>("World"));
        auto&     parent = world.addChild(world.root(), std::make_unique<Node3D>("Parent"));
        auto&     child  = world.addChild(world.root(), std::make_unique<Node3D>("Child"));
        child.setLocalTransform(glm::translate(glm::mat4(1), {1, 0, 0}));
        world.deferCommand(
            [id = parent.id()](SceneTree& scene)
            {
                static_cast<Node3D&>(*scene.find(id)).setLocalTransform(glm::translate(glm::mat4(1), {4, 0, 0}));
            });
        world.deferReparent(child.id(), parent.id());
        auto       born   = std::make_unique<Node>("Born");
        const auto bornId = born->id();
        world.deferCommand(
            [bornId](SceneTree& scene)
            {
                require(!scene.find(bornId), "Deferred child attachment ran ahead of an earlier native command");
            });
        world.deferAddChild(world.root().id(), std::move(born));
        world.deferCommand(
            [bornId](SceneTree& scene)
            {
                require(scene.find(bornId), "Deferred child attachment ran after a later native command");
            });
        world.flushDeferred();
        require(child.parent() == &parent && child.globalTransform()[3].x == 1,
                "Mixed deferred commands did not preserve enqueue order and the child's world transform");
    }

    void nativePauseModes()
    {
        SceneTree  world(std::make_unique<Node>("World"));
        const auto pausable   = world.createEntity("Pausable");
        const auto always     = world.createEntity("Always");
        const auto whenPaused = world.createEntity("WhenPaused");
        const auto disabled   = world.createEntity("Disabled");
        for (const auto id : {pausable, always, whenPaused, disabled})
        {
            world.registry().emplace<SceneTransform>(world.entity(id));
        }
        for (const auto& [id, mode] : {std::pair {always, ProcessMode::eAlways},
                                       std::pair {whenPaused, ProcessMode::eWhenPaused},
                                       std::pair {disabled, ProcessMode::eDisabled}})
        {
            world.registry().patch<SceneProcess>(world.entity(id),
                                                 [mode](SceneProcess& value)
                                                 {
                                                     value.mode = mode;
                                                 });
        }
        const auto system =
            world.addSystem(ScenePhase::ePhysics,
                            [](SceneTree& scene, double)
                            {
                                bool changed = false;
                                for (const auto& [entity, transform] : scene.registry().view<SceneTransform>().each())
                                {
                                    if (scene.canProcess(entity))
                                    {
                                        transform.local[3].x += 1;
                                        changed = true;
                                    }
                                }
                                if (changed)
                                {
                                    scene.markChanged(SceneChange::eTransform);
                                }
                            });
        const auto position = [&world](ObjectId id)
        {
            return world.registry().get<SceneTransform>(world.entity(id)).local[3].x;
        };
        world.physicsStep();
        require(position(pausable) == 1 && position(always) == 1 && position(whenPaused) == 0 &&
                    position(disabled) == 0,
                "Native batch systems did not respect unpaused entity modes");
        world.setPaused(true);
        world.physicsStep();
        require(position(pausable) == 1 && position(always) == 2 && position(whenPaused) == 1 &&
                    position(disabled) == 0,
                "Native batch pause semantics differ from authored node process modes");
        world.stepPaused();
        require(world.paused() && position(pausable) == 2 && position(always) == 3 && position(whenPaused) == 1 &&
                    position(disabled) == 0,
                "Single-step did not share native and node entity-mode semantics");
        world.removeSystem(system);
        world.stop();
    }

    void selectionLifetime()
    {
        SceneTree  world(std::make_unique<Node>("World"));
        const auto camera            = world.createEntity("Camera");
        const auto environment       = world.createEntity("Environment");
        const auto cameraEntity      = world.entity(camera);
        const auto environmentEntity = world.entity(environment);
        world.registry().emplace<SceneCamera>(cameraEntity);
        world.registry().emplace<SceneTransform>(cameraEntity);
        world.registry().emplace<SceneEnvironment>(environmentEntity);
        world.setCurrentCamera(camera);
        world.setCurrentEnvironment(environment);
        world.registry().remove<SceneCamera>(cameraEntity);
        world.registry().remove<SceneEnvironment>(environmentEntity);
        require(!world.currentCamera().value && !world.currentEnvironment().value,
                "Removing a selected component left a live but incorrectly typed selection");
        world.registry().emplace<SceneCamera>(cameraEntity);
        world.registry().emplace<SceneEnvironment>(environmentEntity);
        require(!world.currentCamera().value && !world.currentEnvironment().value,
                "Replacing a component silently restored a retired selection");
        world.setCurrentCamera(camera);
        world.registry().remove<SceneTransform>(cameraEntity);
        require(!world.currentCamera().value, "Removing a camera transform retained an unusable camera selection");
        world.registry().emplace<SceneTransform>(cameraEntity);
        require(!world.currentCamera().value, "Replacing a transform silently restored a retired camera selection");
    }

    void systemIdentityAndMove()
    {
        SceneTree  world(std::make_unique<Node>("World"));
        SceneTree  foreign(std::make_unique<Node>("Foreign"));
        int        calls         = 0;
        const auto ownSystem     = world.addSystem(ScenePhase::eProcess,
                                                   [&calls](SceneTree&, double)
                                                   {
                                                   ++calls;
                                                   });
        const auto foreignSystem = foreign.addSystem(ScenePhase::eProcess,
                                                     [](SceneTree&, double)
                                                     {
                                                     });
        expectError(
            [&]
            {
                world.removeSystem(foreignSystem);
            });
        world.advance(0);
        world.stop();
        require(calls == 1, "Foreign system removal changed this world's callback ownership");
        SceneTree moved(std::move(world));
        moved.removeSystem(ownSystem);
        world = SceneTree(std::make_unique<Node>("Reused"));
        require(world.root().tree() == &world && world.root().name() == "Reused",
                "Moved-from world could not receive a new owned state");
        expectError(
            [&]
            {
                world.removeSystem(ownSystem);
            });
        world.advance(0);
        world.stop();
        foreign.removeSystem(foreignSystem);
    }

    void commandCancellation()
    {
        SceneTree world(std::make_unique<Node>("World"));
        bool      staleCommand = false;
        world.deferCommand(
            [&staleCommand](SceneTree& scene)
            {
                scene.deferCommand(
                    [&staleCommand](SceneTree&)
                    {
                        staleCommand = true;
                    });
                throw std::runtime_error("intentional stopped-world command failure");
            });
        expectError(
            [&]
            {
                world.flushDeferred();
            });
        world.flushDeferred();
        require(!staleCommand, "Failed command on a stopped world survived into the next boundary");
        const auto stopping = world.addSystem(ScenePhase::eProcess,
                                              [](SceneTree& scene, double)
                                              {
                                                  scene.deferCommand(
                                                      [](SceneTree& next)
                                                      {
                                                          next.createEntity("Cancelled");
                                                      });
                                                  scene.stop();
                                              });
        world.advance(0);
        require(!world.running() && world.registry().view<const SceneNode>().size() == 1,
                "Stop from a callback committed pending structural commands");
        world.removeSystem(stopping);
        world.start();
        world.deferCommand(
            [](SceneTree& scene)
            {
                scene.stop();
            });
        world.deferCommand(
            [](SceneTree& scene)
            {
                scene.createEntity("Cancelled");
            });
        world.flushDeferred();
        require(!world.running() && world.registry().view<const SceneNode>().size() == 1,
                "Stop at a command boundary did not cancel later commands");
    }

    class AttachProbe final : public Node3D
    {
    public:
        explicit AttachProbe(bool& fail) :
            Node3D("AttachProbe"),
            m_Fail(fail)
        {
        }

    private:
        void attachComponents(entt::registry& registry, entt::entity entity) override
        {
            registry.emplace<SceneLight>(entity);
            Node3D::attachComponents(registry, entity);
            if (m_Fail)
            {
                throw std::runtime_error("intentional component attachment failure");
            }
        }

        bool& m_Fail;
    };

    void attachmentFailure()
    {
        bool      fail = false;
        SceneTree staging(std::make_unique<Node>("Staging"));
        auto&     branch = staging.addChild(staging.root(), std::make_unique<Node3D>("Branch"));
        staging.addChild(branch, std::make_unique<Node3D>("First"));
        auto&      probe    = staging.addChild(branch, std::make_unique<AttachProbe>(fail));
        const auto probeId  = probe.id();
        const auto branchId = branch.id();
        auto       detached = staging.remove(branch);
        fail                = true;
        SceneTree  destination(std::make_unique<Node>("Destination"));
        const auto document = destination.serialize();
        expectError(
            [&]
            {
                destination.addChild(destination.root(), std::move(detached));
            });
        require(destination.serialize() == document && !destination.usesSceneLighting() &&
                    destination.registry().view<const SceneNode>().size() == 1 &&
                    destination.registry().view<const SceneTransform>().size() == 0 &&
                    destination.entity(probeId) == entt::null && destination.entity(branchId) == entt::null,
                "Failed component attachment left entities, ownership or scene lighting behind");
        const auto raw        = destination.createEntity();
        const auto persistent = destination.registry().get<SceneNode>(destination.entity(raw)).persistentId;
        expectError(
            [&]
            {
                destination.addChild(destination.root(), std::make_unique<Node>("Duplicate", persistent));
            });
        require(destination.entity(persistent) == destination.entity(raw),
                "Authoring attachment overwrote a runtime-only entity's persistent identity");
    }

    void deepHierarchy()
    {
        constexpr size_t      count = 10000;
        SceneTree             world(std::make_unique<Node>("World"));
        std::vector<ObjectId> ids;
        ids.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            const auto id = world.createEntity();
            world.registry().emplace<SceneTransform>(world.entity(id), glm::translate(glm::mat4(1), {1, 0, 0}));
            ids.push_back(id);
        }
        for (size_t index = 0; index + 1 < count; ++index)
        {
            world.setEntityParent(ids[index], ids[index + 1], false);
        }
        world.syncTransforms();
        require(world.registry().get<SceneTransform>(world.entity(ids.front())).global[3].x == float(count),
                "Deep hierarchy did not evaluate parent-first without recursive Node traversal");
        world.queueFree(ids.back());
        require(world.registry().get<SceneProcess>(world.entity(ids.front())).queuedForDeletion,
                "Deep subtree deletion did not mark descendants before the boundary");
        world.flushDeferred();
        require(world.registry().view<const SceneNode>().size() == 1 && world.entity(ids.front()) == entt::null,
                "Deep subtree deletion retained live entities");
    }

    void nativeBatch(size_t count)
    {
        SceneTree             world(std::make_unique<Node>("World"));
        std::vector<ObjectId> ids;
        ids.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            const auto id     = world.createEntity();
            const auto handle = world.entity(id);
            world.registry().emplace<SceneTransform>(handle);
            world.registry().emplace<Velocity>(handle, 3.0f);
            ids.push_back(id);
        }
        require(world.root().children().empty() && !world.find(ids.front()),
                "Native ECS entities allocated Node facades or entered the authored child list");
        const auto system = world.addSystem(ScenePhase::ePhysics,
                                            [](SceneTree& scene, double delta)
                                            {
                                                auto view    = scene.registry().view<SceneTransform, const Velocity>();
                                                bool changed = false;
                                                for (const auto& [entity, transform, velocity] : view.each())
                                                {
                                                    if (scene.canProcess(entity))
                                                    {
                                                        transform.local[3].x += velocity.x * float(delta);
                                                        changed = true;
                                                    }
                                                }
                                                if (changed)
                                                {
                                                    scene.markChanged(SceneChange::eTransform);
                                                }
                                            });
        world.start();
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 60; ++frame)
        {
            world.physicsStep();
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        world.syncTransforms();
        for (const auto& [entity, transform] : world.registry().view<const SceneTransform>().each())
        {
            require(std::abs(transform.global[3].x - 3) < 0.00001f,
                    "Batch system did not execute the shared 60 Hz clock or publish global transforms");
        }
        world.setPaused(true);
        world.physicsStep();
        const auto pausedValue = world.registry().get<SceneTransform>(world.entity(ids.front())).local;
        require(std::abs(pausedValue[3].x - 3) < 0.00001f, "Paused batch system changed simulation state");
        world.stepPaused();
        require(world.registry().get<SceneTransform>(world.entity(ids.front())).local[3].x > pausedValue[3].x,
                "Paused single-step did not execute the batch system");
        world.removeSystem(system);
        expectError(
            [&]
            {
                world.removeSystem(system);
            });
        world.stop();
        expectError(
            [&]
            {
                world.serialize();
            });
        std::cout << "ECS " << count << " entities, 60 fixed ticks: " << elapsed << " ms\n";
    }

    void hierarchyAndCommands()
    {
        SceneTree  world(std::make_unique<Node>("World"));
        SceneTree  foreign(std::make_unique<Node>("Foreign"));
        const auto child  = world.createEntity("Child");
        const auto parent = world.createEntity("Parent");
        world.registry().emplace<SceneTransform>(world.entity(child), glm::translate(glm::mat4(1), {2, 0, 0}));
        world.registry().emplace<SceneTransform>(world.entity(parent), glm::translate(glm::mat4(1), {5, 0, 0}));
        world.setEntityParent(child, parent, false);
        world.syncTransforms();
        require(world.registry().get<SceneTransform>(world.entity(child)).global[3].x == 7,
                "Entity parent ordering depended on creation order");
        expectError(
            [&]
            {
                world.setEntityParent(parent, child);
            });
        expectError(
            [&]
            {
                world.setEntityParent(child, foreign.root().id());
            });
        const auto before = world.changes().transforms;
        world.registry().patch<SceneTransform>(world.entity(parent),
                                               [](SceneTransform& value)
                                               {
                                                   value.local[3].x = 8;
                                               });
        world.syncTransforms();
        require(world.changes().transforms != before &&
                    world.registry().get<SceneTransform>(world.entity(child)).global[3].x == 10,
                "Native component patch did not notify consumers or update dependent transforms");
        world.setEntityParent(child, {}, true);
        world.syncTransforms();
        require(world.registry().get<SceneTransform>(world.entity(child)).global[3].x == 10,
                "Entity reparent did not preserve its global transform");
        world.setEntityParent(child, parent, false);
        const auto retired        = world.entity(child);
        bool       observedQueued = false;
        const auto remove         = world.addSystem(ScenePhase::eProcess,
                                                    [parent](SceneTree& scene, double)
                                                    {
                                                scene.destroyEntity(parent);
                                                scene.deferCommand(
                                                    [](SceneTree& next)
                                                    {
                                                        const auto replacement = next.createEntity("Replacement");
                                                        next.registry().emplace<Velocity>(next.entity(replacement), 9);
                                                    });
                                                    });
        const auto observe        = world.addSystem(ScenePhase::eProcess,
                                                    [&](SceneTree& scene, double)
                                                    {
                                                 observedQueued = scene.entity(child) != entt::null &&
                                                                  !scene.canProcess(scene.entity(child));
                                                    });
        world.advance(0);
        require(observedQueued && world.entity(child) == entt::null && world.entity(parent) == entt::null &&
                    !world.registry().valid(retired) && world.registry().view<const Velocity>().size() == 1,
                "Queued ECS deletion or owned structural command violated the phase boundary");
        world.removeSystem(remove);
        world.removeSystem(observe);
        world.stop();
        SceneTree  moved(std::move(world));
        const auto revision = moved.changes().transforms;
        const auto id       = moved.createEntity();
        moved.registry().emplace<SceneTransform>(moved.entity(id));
        require(moved.changes().transforms > revision,
                "World move retained a signal receiver pointing at the old world");
        expectError(
            [&]
            {
                moved.destroyEntity(child);
            });
        const auto failing = moved.addSystem(ScenePhase::eProcess,
                                             [](SceneTree& scene, double)
                                             {
                                                 scene.deferCommand(
                                                     [](SceneTree&)
                                                     {
                                                         throw std::runtime_error("intentional ECS command failure");
                                                     });
                                             });
        expectError(
            [&]
            {
                moved.advance(0);
            });
        require(!moved.running(), "Failed ECS command left the world partially running");
        moved.removeSystem(failing);
        moved.advance(0);
        require(moved.running(), "World could not restart after a failed structural command");
        moved.stop();
    }

    void facadeData()
    {
        SceneTree world(std::make_unique<Node>("World"));
        auto&     rig   = world.addChild(world.root(), std::make_unique<Node3D>("Rig"));
        auto&     light = world.addChild(rig, std::make_unique<Light3D>("Light", RenderLightKind::ePoint));
        light.setOwner(&rig);
        light.setProcessPriority(-4);
        auto&         camera      = world.addChild(rig, std::make_unique<Camera3D>("Camera"));
        auto&         environment = world.addChild(world.root(), std::make_unique<WorldEnvironment>("Environment"));
        const AssetId model {StableId::generate()};
        auto&         mesh = world.addChild(rig, std::make_unique<MeshInstance3D>("Mesh", model));
        require(&light.settings() == &world.registry().get<SceneLight>(light.entity()).settings &&
                    &camera.settings() == &world.registry().get<SceneCamera>(camera.entity()).settings &&
                    &environment.settings() == &world.registry().get<SceneEnvironment>(environment.entity()).settings &&
                    &mesh.materialOverrides() == &world.registry().get<SceneMesh>(mesh.entity()).materials,
                "Facades retain duplicate runtime rendering data");
        world.registry().patch<SceneLight>(light.entity(),
                                           [](SceneLight& value)
                                           {
                                               value.settings.intensity = 12;
                                           });
        world.registry().patch<SceneProcess>(light.entity(),
                                             [](SceneProcess& value)
                                             {
                                                 value.priority = -7;
                                             });
        require(light.settings().intensity == 12 && light.processPriority() == -7 &&
                    world.registry().get<SceneHierarchy>(light.entity()).owner == rig.entity(),
                "Native ECS edits did not reach the script/authoring facade");
        auto detached = world.remove(rig);
        require(light.settings().intensity == 12 && light.processPriority() == -7 && light.owner() == detached.get(),
                "Subtree detach lost component state or authoring ownership");
        world.addChild(world.root(), std::move(detached));
        require(light.settings().intensity == 12 && light.processPriority() == -7 && light.parent() == &rig,
                "Subtree reattachment did not restore component state");
        const auto document = world.serialize();
        auto       restored = SceneTree::parse(document);
        require(restored.serialize() == document, "ECS-backed authored state changed its version-1 JSON round trip");
    }
} // namespace

int main()
{
    nativePauseModes();
    lifecycleCancellation();
    deferredOrdering();
    selectionLifetime();
    systemIdentityAndMove();
    commandCancellation();
    attachmentFailure();
    deepHierarchy();
    facadeData();
    hierarchyAndCommands();
    for (const auto count : {1000u, 10000u, 100000u})
    {
        nativeBatch(count);
    }
}
