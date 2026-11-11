#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/platform/input/input.hpp>

#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vultra
{
    using InputBinding = std::variant<KeyCode, MouseCode>;

    struct ActionState
    {
        bool held     = false;
        bool pressed  = false;
        bool released = false;
    };

    // Each routing owner owns its map. Raw state and UI capture remain independent.
    class InputMap
    {
    public:
        [[nodiscard]] std::expected<void, Error> bind(std::string action, InputBinding binding);
        [[nodiscard]] std::expected<void, Error> remove(std::string_view action);
        // Rejects injected events with invalid codes before applying any of them.
        [[nodiscard]] std::expected<void, Error> update(std::span<const InputEvent> events, InputCapture capture = {});
        [[nodiscard]] std::expected<ActionState, Error> action(std::string_view name) const;
        // Match one raw event, independently of aggregate frame edges and UI ownership.
        [[nodiscard]] std::expected<ActionState, Error> eventAction(std::string_view  name,
                                                                    const InputEvent& event) const;
        // Publish accumulated edges once, before each fixed step; catch-up steps do not repeat them.
        void                                                beginPhysicsTick();
        [[nodiscard]] std::expected<ActionState, Error>     physicsAction(std::string_view name) const;
        std::string                                         serialize() const;
        [[nodiscard]] static std::expected<InputMap, Error> parse(std::string_view json);

    private:
        struct Entry
        {
            std::vector<InputBinding> bindings;
            ActionState               frame;
            ActionState               pending;
            ActionState               physics;
        };

        void                                        refreshActions();
        std::map<std::string, Entry, std::less<>>   m_Actions;
        std::array<bool, size_t(KeyCode::eCount)>   m_Keys {};
        std::array<bool, size_t(MouseCode::eCount)> m_Buttons {};
    };
} // namespace vultra
