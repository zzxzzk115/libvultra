#include <vultra/platform/input/input_map.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::string_view kKeys[] {"Unknown",     "A",
                                            "B",           "C",
                                            "D",           "E",
                                            "F",           "G",
                                            "H",           "I",
                                            "J",           "K",
                                            "L",           "M",
                                            "N",           "O",
                                            "P",           "Q",
                                            "R",           "S",
                                            "T",           "U",
                                            "V",           "W",
                                            "X",           "Y",
                                            "Z",           "Num0",
                                            "Num1",        "Num2",
                                            "Num3",        "Num4",
                                            "Num5",        "Num6",
                                            "Num7",        "Num8",
                                            "Num9",        "Return",
                                            "Escape",      "Backspace",
                                            "Tab",         "Space",
                                            "Minus",       "Equals",
                                            "LeftBracket", "RightBracket",
                                            "Backslash",   "Semicolon",
                                            "Apostrophe",  "Grave",
                                            "Comma",       "Period",
                                            "Slash",       "CapsLock",
                                            "F1",          "F2",
                                            "F3",          "F4",
                                            "F5",          "F6",
                                            "F7",          "F8",
                                            "F9",          "F10",
                                            "F11",         "F12",
                                            "PrintScreen", "ScrollLock",
                                            "Pause",       "Insert",
                                            "Home",        "PageUp",
                                            "Delete",      "End",
                                            "PageDown",    "Right",
                                            "Left",        "Down",
                                            "Up",          "NumLock",
                                            "KPDivide",    "KPMultiply",
                                            "KPMinus",     "KPPlus",
                                            "KPEnter",     "KP0",
                                            "KP1",         "KP2",
                                            "KP3",         "KP4",
                                            "KP5",         "KP6",
                                            "KP7",         "KP8",
                                            "KP9",         "KPPeriod",
                                            "LCtrl",       "LShift",
                                            "LAlt",        "LGUI",
                                            "RCtrl",       "RShift",
                                            "RAlt",        "RGUI",
                                            "Menu"};
        constexpr std::string_view kButtons[] {"Left", "Middle", "Right", "X1", "X2"};
        static_assert(std::size(kKeys) == size_t(KeyCode::eCount));
        static_assert(std::size(kButtons) == size_t(MouseCode::eCount));
    } // namespace

    void InputMap::bind(std::string name, InputBinding binding)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Input action requires a nonempty name");
        }
        std::visit(
            [](auto code)
            {
                using Code = decltype(code);
                if constexpr (std::is_same_v<Code, KeyCode>)
                {
                    if (code == KeyCode::eUnknown || code >= KeyCode::eCount)
                    {
                        throw std::invalid_argument("Invalid action key");
                    }
                }
                else if (code >= MouseCode::eCount)
                {
                    throw std::invalid_argument("Invalid action mouse button");
                }
            },
            binding);
        auto& entry = m_Actions[name];
        if (std::ranges::find(entry.bindings, binding) != entry.bindings.end())
        {
            throw std::invalid_argument("Duplicate input binding: " + name);
        }
        entry.bindings.push_back(binding);
        refreshActions();
    }

    void InputMap::remove(std::string_view name)
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            throw std::invalid_argument("Unknown input action: " + std::string(name));
        }
        m_Actions.erase(found);
    }

    void InputMap::refreshActions()
    {
        for (auto& [name, entry] : m_Actions)
        {
            const bool held = std::ranges::any_of(entry.bindings,
                                                  [&](const auto& binding)
                                                  {
                                                      return std::visit(
                                                          [&](auto code)
                                                          {
                                                              if constexpr (std::is_same_v<decltype(code), KeyCode>)
                                                              {
                                                                  return m_Keys[size_t(code)];
                                                              }
                                                              else
                                                              {
                                                                  return m_Buttons[size_t(code)];
                                                              }
                                                          },
                                                          binding);
                                                  });
            entry.frame.pressed |= held && !entry.frame.held;
            entry.frame.released |= !held && entry.frame.held;
            entry.pending.pressed |= held && !entry.frame.held;
            entry.pending.released |= !held && entry.frame.held;
            entry.frame.held   = held;
            entry.pending.held = held;
        }
    }

    void InputMap::update(std::span<const InputEvent> events, InputCapture capture)
    {
        for (auto& [name, entry] : m_Actions)
        {
            entry.frame.pressed  = false;
            entry.frame.released = false;
        }
        for (const auto& event : events)
        {
            std::visit(
                [&](const auto& value)
                {
                    using Event = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Event, KeyInput>)
                    {
                        if (value.key == KeyCode::eUnknown || value.key >= KeyCode::eCount)
                        {
                            throw std::invalid_argument("Invalid injected action key");
                        }
                        if (value.action == InputAction::eRelease)
                        {
                            m_Keys[size_t(value.key)] = false;
                        }
                        else if (value.action == InputAction::ePress && !inputCaptured(event, capture))
                        {
                            m_Keys[size_t(value.key)] = true;
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseButtonInput>)
                    {
                        if (value.button >= MouseCode::eCount)
                        {
                            throw std::invalid_argument("Invalid injected action mouse button");
                        }
                        if (!value.held || !inputCaptured(event, capture))
                        {
                            m_Buttons[size_t(value.button)] = value.held;
                        }
                    }
                    else if constexpr (std::is_same_v<Event, FocusInput>)
                    {
                        if (!value.focused)
                        {
                            m_Keys.fill(false);
                            m_Buttons.fill(false);
                        }
                    }
                },
                event.value);
            refreshActions();
        }
    }

    ActionState InputMap::action(std::string_view name) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            throw std::invalid_argument("Unknown input action: " + std::string(name));
        }
        return found->second.frame;
    }

    ActionState InputMap::eventAction(std::string_view name, const InputEvent& event) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            throw std::invalid_argument("Unknown input action: " + std::string(name));
        }
        return std::visit(
            [&](const auto& value) -> ActionState
            {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, KeyInput>)
                {
                    if (std::ranges::find(found->second.bindings, InputBinding(value.key)) !=
                        found->second.bindings.end())
                    {
                        return {value.action != InputAction::eRelease,
                                value.action == InputAction::ePress,
                                value.action == InputAction::eRelease};
                    }
                }
                else if constexpr (std::is_same_v<Event, MouseButtonInput>)
                {
                    if (std::ranges::find(found->second.bindings, InputBinding(value.button)) !=
                        found->second.bindings.end())
                    {
                        return {value.held, value.held, !value.held};
                    }
                }
                return {};
            },
            event.value);
    }

    void InputMap::beginPhysicsTick()
    {
        for (auto& [name, entry] : m_Actions)
        {
            entry.physics          = entry.pending;
            entry.pending.pressed  = false;
            entry.pending.released = false;
        }
    }

    ActionState InputMap::physicsAction(std::string_view name) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            throw std::invalid_argument("Unknown input action: " + std::string(name));
        }
        return found->second.physics;
    }

    std::string InputMap::serialize() const
    {
        Json actions = Json::object();
        for (const auto& [name, entry] : m_Actions)
        {
            auto bindings = Json::array();
            for (const auto& binding : entry.bindings)
            {
                bindings.push_back(std::visit(
                    [](auto code) -> Json
                    {
                        if constexpr (std::is_same_v<decltype(code), KeyCode>)
                        {
                            return {{"key", kKeys[size_t(code)]}};
                        }
                        else
                        {
                            return {{"mouse", kButtons[size_t(code)]}};
                        }
                    },
                    binding));
            }
            actions[name] = std::move(bindings);
        }
        return Json {{"format", "vultra.input_map"}, {"version", 1}, {"actions", std::move(actions)}}.dump(2) + "\n";
    }

    InputMap InputMap::parse(std::string_view json)
    {
        const auto data = Json::parse(json);
        if (data.at("format") != "vultra.input_map" || data.at("version") != 1 || data.size() != 3 ||
            !data.at("actions").is_object())
        {
            throw std::invalid_argument("Invalid input map format/version");
        }
        InputMap result;
        for (const auto& [name, bindings] : data.at("actions").items())
        {
            if (!bindings.is_array() || bindings.empty())
            {
                throw std::invalid_argument("Input action requires an array of bindings: " + name);
            }
            for (const auto& binding : bindings)
            {
                if (!binding.is_object() || binding.size() != 1)
                {
                    throw std::invalid_argument("Input binding requires one key or mouse button");
                }
                if (binding.contains("key"))
                {
                    const auto key = std::ranges::find(kKeys, binding.at("key").get<std::string>());
                    if (key == std::end(kKeys))
                    {
                        throw std::invalid_argument("Unknown action key");
                    }
                    result.bind(name, KeyCode(key - std::begin(kKeys)));
                }
                else if (binding.contains("mouse"))
                {
                    const auto button = std::ranges::find(kButtons, binding.at("mouse").get<std::string>());
                    if (button == std::end(kButtons))
                    {
                        throw std::invalid_argument("Unknown action mouse button");
                    }
                    result.bind(name, MouseCode(button - std::begin(kButtons)));
                }
                else
                {
                    throw std::invalid_argument("Unknown input binding kind");
                }
            }
        }
        return result;
    }
} // namespace vultra
