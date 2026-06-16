#include "vultra/core/input/input_action.hpp"

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

namespace vultra
{
    namespace
    {
        // The JSON uses stripped enum names ("Space", "South", "LeftX") matching the
        // Lua-facing tables (stripE). The C++ enumerators carry an 'e' prefix, so map
        // by prepending it.
        template<typename Enum>
        bool enumFromName(const std::string& name, Enum& out)
        {
            if (auto value = magic_enum::enum_cast<Enum>("e" + name))
            {
                out = *value;
                return true;
            }
            return false;
        }

        // Enum value -> stripped name ("eSpace" -> "Space"), matching the JSON/Lua tables.
        template<typename Enum>
        std::string enumName(Enum value)
        {
            const auto name = magic_enum::enum_name(value);
            return name.size() > 1 && name.front() == 'e' ? std::string(name.substr(1)) : std::string(name);
        }

        bool eventFromJson(const nlohmann::json& j, InputActionEvent& out)
        {
            out.scale = j.value("scale", 1.0f);
            if (j.contains("key"))
            {
                out.kind = InputActionEvent::Kind::eKey;
                return enumFromName(j["key"].get<std::string>(), out.key);
            }
            if (j.contains("mouseButton"))
            {
                out.kind = InputActionEvent::Kind::eMouseButton;
                return enumFromName(j["mouseButton"].get<std::string>(), out.mouseButton);
            }
            if (j.contains("gamepadButton"))
            {
                out.kind = InputActionEvent::Kind::eGamepadButton;
                return enumFromName(j["gamepadButton"].get<std::string>(), out.gamepadButton);
            }
            if (j.contains("gamepadAxis"))
            {
                out.kind = InputActionEvent::Kind::eGamepadAxis;
                return enumFromName(j["gamepadAxis"].get<std::string>(), out.gamepadAxis);
            }
            return false;
        }
    } // namespace

    bool parseInputActionMapJson(std::string_view text, InputActionMap& outMap, std::string* outError)
    {
        nlohmann::json root;
        try
        {
            root = nlohmann::json::parse(text);
        }
        catch (const std::exception& e)
        {
            if (outError)
                *outError = e.what();
            return false;
        }

        const auto actionsIt = root.find("actions");
        if (actionsIt == root.end() || !actionsIt->is_object())
        {
            if (outError)
                *outError = "missing 'actions' object";
            return false;
        }

        InputActionMap parsed;
        for (const auto& [name, def] : actionsIt->items())
        {
            InputActionDef action;
            action.deadzone = def.value("deadzone", 0.5f);
            if (def.contains("events") && def["events"].is_array())
            {
                for (const auto& ev : def["events"])
                {
                    InputActionEvent event;
                    if (eventFromJson(ev, event))
                        action.events.push_back(event);
                }
            }
            parsed.emplace(name, std::move(action));
        }

        outMap = std::move(parsed);
        return true;
    }

    std::string inputActionMapToJson(const InputActionMap& map)
    {
        // Stable, sorted output so diffs are clean.
        std::vector<std::string> names;
        names.reserve(map.size());
        for (const auto& [name, action] : map)
            names.push_back(name);
        std::sort(names.begin(), names.end());

        nlohmann::json actions = nlohmann::json::object();
        for (const auto& name : names)
        {
            const auto&    action = map.at(name);
            nlohmann::json events = nlohmann::json::array();
            for (const auto& ev : action.events)
            {
                nlohmann::json e = nlohmann::json::object();
                switch (ev.kind)
                {
                    case InputActionEvent::Kind::eKey:
                        e["key"] = enumName(ev.key);
                        break;
                    case InputActionEvent::Kind::eMouseButton:
                        e["mouseButton"] = enumName(ev.mouseButton);
                        break;
                    case InputActionEvent::Kind::eGamepadButton:
                        e["gamepadButton"] = enumName(ev.gamepadButton);
                        break;
                    case InputActionEvent::Kind::eGamepadAxis:
                        e["gamepadAxis"] = enumName(ev.gamepadAxis);
                        break;
                }
                if (ev.scale != 1.0f)
                    e["scale"] = ev.scale;
                events.push_back(std::move(e));
            }
            actions[name] = {{"deadzone", action.deadzone}, {"events", std::move(events)}};
        }

        return nlohmann::json {{"actions", std::move(actions)}}.dump(2);
    }
} // namespace vultra
