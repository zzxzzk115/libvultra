#include <vultra/platform/input/input_map.hpp>
#include <vultra/scene/scene_api.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    Input    input;
    InputMap map;
    map.bind("jump", KeyCode::eSpace);
    map.bind("jump", MouseCode::eRight);
    input.setKey(KeyCode::eSpace, InputAction::ePress);
    input.addText("hello");
    input.setKey(KeyCode::eSpace, InputAction::eRelease);
    input.advanceFrame();
    require(input.events().size() == 3 && std::holds_alternative<TextInput>(input.events()[1].value),
            "Backend event order was lost");
    map.update(input.events());
    require(map.action("jump").pressed && map.action("jump").released && !map.action("jump").held,
            "Quick action press/release was lost");
    input.advanceFrame();
    map.update(input.events());
    require(!map.action("jump").pressed, "Frame edge repeated without an event");
    map.beginPhysicsTick();
    require(map.physicsAction("jump").pressed && map.physicsAction("jump").released,
            "Edges disappeared before the next fixed tick");
    map.beginPhysicsTick();
    require(!map.physicsAction("jump").pressed && !map.physicsAction("jump").released,
            "Catch-up physics repeated action edges");

    input.setKey(KeyCode::eSpace, InputAction::ePress);
    input.advanceFrame();
    input.events()[0].consumed = true;
    map.update(input.events());
    require(!map.action("jump").held && input.isKeyHeld(KeyCode::eSpace),
            "UI consumption erased raw input or leaked an action press");
    input.setKey(KeyCode::eSpace, InputAction::eRelease);
    input.setKey(KeyCode::eSpace, InputAction::ePress);
    input.setMouseButton(MouseCode::eRight, true);
    input.advanceFrame();
    map.update(input.events());
    require(map.action("jump").held && map.action("jump").pressed, "Action did not combine keyboard/mouse bindings");
    input.setKey(KeyCode::eSpace, InputAction::eRelease);
    input.advanceFrame();
    map.update(input.events(), {.keyboard = true});
    require(map.action("jump").held && !map.action("jump").released, "Releasing one binding cancelled another");
    input.setMouseButton(MouseCode::eRight, false);
    input.advanceFrame();
    input.events()[0].consumed = true;
    map.update(input.events(), {.mouse = true});
    require(!map.action("jump").held && map.action("jump").released, "Captured release left the action stuck");
    input.setKey(KeyCode::eSpace, InputAction::ePress);
    input.advanceFrame();
    map.update(input.events());
    input.setFocused(false);
    input.advanceFrame();
    map.update(input.events());
    require(!map.action("jump").held && map.action("jump").released, "Focus loss left a held action");

    const auto json     = map.serialize();
    auto       restored = InputMap::parse(json);
    require(restored.serialize() == json && !restored.action("jump").held,
            "Input configuration persisted transient state or changed its named bindings");
    const std::array bad {
        R"({"format":"vultra.input_map","version":2,"actions":{}})",
        R"({"format":"vultra.input_map","version":1,"actions":{"jump":[{"key":"bad"}]}})",
        R"({"format":"vultra.input_map","version":1,"actions":{"jump":[{"key":"Space"},{"key":"Space"}]}})"};
    for (const auto* text : bad)
    {
        bool rejected = false;
        try
        {
            InputMap::parse(text);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Invalid action configuration was accepted");
    }
    SceneTree world(std::make_unique<Node>("Input world"));
    world.input().bind("jump", KeyCode::eSpace);
    Input replay;
    replay.setKey(KeyCode::eSpace, InputAction::ePress);
    replay.advanceFrame();
    world.input().update(replay.events());
    std::vector<ActionState> fixed;
    std::vector<ActionState> variable;
    world.addSystem(ScenePhase::ePhysics,
                    [&](SceneTree& scene, double)
                    {
                        fixed.push_back(scene.action("jump"));
                    });
    world.addSystem(ScenePhase::eProcess,
                    [&](SceneTree& scene, double)
                    {
                        variable.push_back(scene.action("jump"));
                    });
    world.advance(world.fixedDelta() * 3.1);
    require(fixed.size() == 3 && fixed[0].pressed && !fixed[1].pressed && !fixed[2].pressed && variable.size() == 1 &&
                variable[0].pressed && sceneActionState(world, "jump") == 3,
            "Scene callbacks did not select frame/fixed input edges consistently");
    world.stop();
    SceneTree moved(std::move(world));
    require(moved.input().action("jump").held, "Stopped-world moves lost the owned action map");
    std::cout << "Input map tests passed: ordered headless events, capture, release and fixed-step edges\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
