#include <vultra/platform/input/input_map.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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

    std::expected<void, Error> InputMap::bind(std::string name, InputBinding binding)
    {
        if (name.empty())
        {
            return makeError("Input action requires a nonempty name");
        }
        if (const auto* key = std::get_if<KeyCode>(&binding);
            key && (*key == KeyCode::eUnknown || *key >= KeyCode::eCount))
        {
            return makeError("Invalid action key");
        }
        if (const auto* button = std::get_if<MouseCode>(&binding); button && *button >= MouseCode::eCount)
        {
            return makeError("Invalid action mouse button");
        }
        const auto existing = m_Actions.find(name);
        if (existing != m_Actions.end() &&
            std::ranges::find(existing->second.bindings, binding) != existing->second.bindings.end())
        {
            return makeError("Duplicate input binding: {}", name);
        }
        m_Actions[name].bindings.push_back(binding);
        refreshActions();
        return {};
    }

    std::expected<void, Error> InputMap::remove(std::string_view name)
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            return makeError("Unknown input action: {}", name);
        }
        m_Actions.erase(found);
        return {};
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

    std::expected<void, Error> InputMap::update(std::span<const InputEvent> events, InputCapture capture)
    {
        for (const auto& event : events)
        {
            if (const auto* key = std::get_if<KeyInput>(&event.value);
                key && (key->key == KeyCode::eUnknown || key->key >= KeyCode::eCount))
            {
                return makeError("Invalid injected action key");
            }
            if (const auto* button = std::get_if<MouseButtonInput>(&event.value);
                button && button->button >= MouseCode::eCount)
            {
                return makeError("Invalid injected action mouse button");
            }
        }
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
        return {};
    }

    std::expected<ActionState, Error> InputMap::action(std::string_view name) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            return makeError("Unknown input action: {}", name);
        }
        return found->second.frame;
    }

    std::expected<ActionState, Error> InputMap::eventAction(std::string_view name, const InputEvent& event) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            return makeError("Unknown input action: {}", name);
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

    std::expected<ActionState, Error> InputMap::physicsAction(std::string_view name) const
    {
        const auto found = m_Actions.find(name);
        if (found == m_Actions.end())
        {
            return makeError("Unknown input action: {}", name);
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

    std::expected<InputMap, Error> InputMap::parse(std::string_view json)
    {
        const auto data = Json::parse(json, nullptr, false);
        if (data.is_discarded() || !data.is_object() || data.size() != 3 || !data.contains("format") ||
            data.at("format") != "vultra.input_map" || !data.contains("version") || data.at("version") != 1 ||
            !data.contains("actions") || !data.at("actions").is_object())
        {
            return makeError("Invalid input map format/version");
        }
        InputMap result;
        for (const auto& [name, bindings] : data.at("actions").items())
        {
            if (!bindings.is_array() || bindings.empty())
            {
                return makeError("Input action requires an array of bindings: {}", name);
            }
            for (const auto& binding : bindings)
            {
                if (!binding.is_object() || binding.size() != 1)
                {
                    return makeError("Input binding requires one key or mouse button");
                }
                InputBinding code;
                if (binding.contains("key"))
                {
                    const auto& value = binding.at("key");
                    const auto  key =
                        value.is_string() ? std::ranges::find(kKeys, value.get<std::string>()) : std::end(kKeys);
                    if (key == std::end(kKeys))
                    {
                        return makeError("Unknown action key");
                    }
                    code = KeyCode(key - std::begin(kKeys));
                }
                else if (binding.contains("mouse"))
                {
                    const auto& value = binding.at("mouse");
                    const auto  button =
                        value.is_string() ? std::ranges::find(kButtons, value.get<std::string>()) : std::end(kButtons);
                    if (button == std::end(kButtons))
                    {
                        return makeError("Unknown action mouse button");
                    }
                    code = MouseCode(button - std::begin(kButtons));
                }
                else
                {
                    return makeError("Unknown input binding kind");
                }
                if (auto bound = result.bind(name, code); !bound)
                {
                    return std::unexpected(bound.error());
                }
            }
        }
        return result;
    }
} // namespace vultra
