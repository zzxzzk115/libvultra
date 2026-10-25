#pragma once

#include <vultra/platform/input/input.hpp>

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
        void        bind(std::string action, InputBinding binding);
        void        remove(std::string_view action);
        void        update(std::span<const InputEvent> events, InputCapture capture = {});
        ActionState action(std::string_view name) const;
        // Match one raw event, independently of aggregate frame edges and UI ownership.
        ActionState eventAction(std::string_view name, const InputEvent& event) const;
        // Publish accumulated edges once, before each fixed step; catch-up steps do not repeat them.
        void            beginPhysicsTick();
        ActionState     physicsAction(std::string_view name) const;
        std::string     serialize() const;
        static InputMap parse(std::string_view json);

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
