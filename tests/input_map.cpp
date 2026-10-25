#include <vultra/platform/input/input_map.hpp>

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

    void eventSnapshots()
    {
        using namespace vultra;
        Input input;
        input.setKey(KeyCode::eLShift, InputAction::ePress);
        input.setKey(KeyCode::eRCtrl, InputAction::ePress);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        input.setMousePosition({12, 24});
        input.setMouseButton(MouseCode::eLeft, true);
        input.setMousePosition({18, 20});
        input.addMouseScroll({0, -1});
        input.setKey(KeyCode::eLShift, InputAction::eRelease);
        input.setMouseButton(MouseCode::eLeft, false);
        input.setKey(KeyCode::eRCtrl, InputAction::eRelease);
        input.advanceFrame();
        const auto events = input.events();
        require(events.size() == 10, "Backend input events were coalesced or reordered");
        const auto shift   = uint32_t(InputModifier::eShift);
        const auto control = uint32_t(InputModifier::eControl);
        require(std::get<KeyInput>(events[0].value).modifiers == shift &&
                    std::get<KeyInput>(events[2].value).modifiers == (shift | control) &&
                    std::get<KeyInput>(events[7].value).modifiers == control &&
                    std::get<KeyInput>(events[9].value).modifiers == 0,
                "Events did not retain the modifiers active at their position in the backend stream");
        const auto& initial  = std::get<MouseMotionInput>(events[3].value);
        const auto& button   = std::get<MouseButtonInput>(events[4].value);
        const auto& motion   = std::get<MouseMotionInput>(events[5].value);
        const auto& scroll   = std::get<MouseScrollInput>(events[6].value);
        const auto& released = std::get<MouseButtonInput>(events[8].value);
        require(initial.delta == glm::vec2(0) && button.position == glm::vec2(12, 24) &&
                    button.modifiers == (shift | control) && motion.delta == glm::vec2(6, -4) &&
                    motion.position == glm::vec2(18, 20) && scroll.position == motion.position &&
                    scroll.delta == glm::vec2(0, -1) && scroll.modifiers == (shift | control) &&
                    released.position == motion.position && released.modifiers == control &&
                    input.mousePositionDelta() == motion.delta,
                "Mouse events sampled the final frame position/modifiers or lost their individual deltas");
        require(inputCaptured(events[2], {.keyboard = true}) && !inputCaptured(events[2], {.mouse = true}) &&
                    inputCaptured(events[4], {.mouse = true}) && !inputCaptured(events[4], {.keyboard = true}),
                "Capture does not distinguish keyboard and mouse ownership");

        input.setFocused(false);
        input.setFocused(true);
        input.setMousePosition({200, 300});
        input.setMousePosition({202, 303});
        input.advanceFrame();
        require(input.events().size() == 4 && !inputCaptured(input.events()[0], {.mouse = true, .keyboard = true}) &&
                    std::get<MouseMotionInput>(input.events()[2].value).delta == glm::vec2(0) &&
                    std::get<MouseMotionInput>(input.events()[3].value).delta == glm::vec2(2, 3) &&
                    std::get<MouseMotionInput>(input.events()[3].value).modifiers == 0,
                "Focus reset leaked old modifiers, created a mouse jump or was swallowed by UI capture");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    eventSnapshots();
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
    std::cout << "Input map tests passed: ordered headless events, capture, release and fixed-step edges\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
