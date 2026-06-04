#include "vultra/function/animation/animator_graph.hpp"

#include <vbase/core/uuid.hpp>

#include <algorithm>
#include <unordered_set>

namespace vultra::animator_graph
{
    namespace
    {
        CoreUUID uuidFromString(const std::string& str)
        {
            vbase::UUID tmp {};
            if (!str.empty())
                vbase::try_parse_uuid(str.c_str(), tmp);
            return CoreUUID {tmp};
        }

        ParameterType parameterTypeFromString(const std::string& s)
        {
            if (s == "bool")
                return ParameterType::eBool;
            if (s == "trigger")
                return ParameterType::eTrigger;
            return ParameterType::eFloat;
        }
        const char* parameterTypeToString(ParameterType t)
        {
            switch (t)
            {
                case ParameterType::eBool:
                    return "bool";
                case ParameterType::eTrigger:
                    return "trigger";
                default:
                    return "float";
            }
        }

        ConditionType conditionTypeFromString(const std::string& s)
        {
            if (s == "less" || s == "<")
                return ConditionType::eLess;
            if (s == "equal" || s == "==")
                return ConditionType::eEqual;
            if (s == "notEqual" || s == "!=")
                return ConditionType::eNotEqual;
            if (s == "true" || s == "isTrue")
                return ConditionType::eTrue;
            if (s == "false" || s == "isFalse")
                return ConditionType::eFalse;
            if (s == "trigger" || s == "set")
                return ConditionType::eTrigger;
            return ConditionType::eGreater;
        }
        const char* conditionTypeToString(ConditionType t)
        {
            switch (t)
            {
                case ConditionType::eLess:
                    return "less";
                case ConditionType::eEqual:
                    return "equal";
                case ConditionType::eNotEqual:
                    return "notEqual";
                case ConditionType::eTrue:
                    return "true";
                case ConditionType::eFalse:
                    return "false";
                case ConditionType::eTrigger:
                    return "trigger";
                default:
                    return "greater";
            }
        }

        Transition transitionFromJson(const nlohmann::json& j)
        {
            Transition t;
            t.to          = j.value("to", std::string {});
            t.duration    = j.value("duration", 0.2f);
            t.hasExitTime = j.value("hasExitTime", false);
            t.exitTime    = j.value("exitTime", 1.0f);
            if (auto it = j.find("conditions"); it != j.end() && it->is_array())
            {
                for (const auto& c : *it)
                {
                    Condition cond;
                    cond.parameter = c.value("parameter", std::string {});
                    cond.type      = conditionTypeFromString(c.value("type", std::string {"greater"}));
                    cond.threshold = c.value("threshold", c.value("value", 0.0f));
                    t.conditions.push_back(std::move(cond));
                }
            }
            return t;
        }

        nlohmann::json transitionToJson(const Transition& t)
        {
            nlohmann::json conditions = nlohmann::json::array();
            for (const auto& c : t.conditions)
                conditions.push_back({{"parameter", c.parameter},
                                      {"type", conditionTypeToString(c.type)},
                                      {"threshold", c.threshold}});
            return {{"to", t.to},
                    {"duration", t.duration},
                    {"hasExitTime", t.hasExitTime},
                    {"exitTime", t.exitTime},
                    {"conditions", std::move(conditions)}};
        }
    } // namespace

    const State* Graph::findState(std::string_view name) const
    {
        for (const auto& s : states)
            if (s.name == name)
                return &s;
        return nullptr;
    }

    int Graph::stateIndex(std::string_view name) const
    {
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    const Parameter* Graph::findParameter(std::string_view name) const
    {
        for (const auto& p : parameters)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    std::optional<Graph> graphFromJson(const nlohmann::json& root, std::vector<std::string>* diagnostics)
    {
        const auto fail = [&](const std::string& message) -> std::optional<Graph> {
            if (diagnostics)
                diagnostics->push_back(message);
            return std::nullopt;
        };

        if (!root.is_object())
            return fail("Animator graph root must be an object.");

        try
        {
        Graph graph;
        graph.version  = root.value("version", 1u);
        graph.name     = root.value("name", std::string {});
        graph.entry    = root.value("entry", root.value("entryState", std::string {}));
        graph.metadata = root.value("metadata", nlohmann::json::object());

        if (auto it = root.find("parameters"); it != root.end() && it->is_array())
        {
            for (const auto& p : *it)
            {
                Parameter param;
                param.name = p.value("name", std::string {});
                param.type = parameterTypeFromString(p.value("type", std::string {"float"}));
                // "default" may be a number or a boolean depending on the parameter type;
                // read it by JSON type to avoid nlohmann type_error on mismatch.
                if (auto d = p.find("default"); d != p.end())
                {
                    if (d->is_boolean())
                        param.defaultBool = d->get<bool>();
                    else if (d->is_number())
                        param.defaultFloat = d->get<float>();
                }
                if (auto d = p.find("defaultBool"); d != p.end() && d->is_boolean())
                    param.defaultBool = d->get<bool>();
                if (auto d = p.find("defaultFloat"); d != p.end() && d->is_number())
                    param.defaultFloat = d->get<float>();
                if (!param.name.empty())
                    graph.parameters.push_back(std::move(param));
            }
        }

        if (auto it = root.find("states"); it != root.end() && it->is_array())
        {
            for (const auto& s : *it)
            {
                State state;
                state.name      = s.value("name", std::string {});
                state.animation = uuidFromString(s.value("animation", std::string {}));
                state.speed     = s.value("speed", 1.0f);
                state.loop      = s.value("loop", true);
                state.editor    = s.value("editor", nlohmann::json::object());
                if (auto tr = s.find("transitions"); tr != s.end() && tr->is_array())
                    for (const auto& t : *tr)
                        state.transitions.push_back(transitionFromJson(t));
                if (!state.name.empty())
                    graph.states.push_back(std::move(state));
            }
        }

        if (auto it = root.find("anyTransitions"); it != root.end() && it->is_array())
            for (const auto& t : *it)
                graph.anyTransitions.push_back(transitionFromJson(t));

        if (graph.entry.empty() && !graph.states.empty())
            graph.entry = graph.states.front().name;

        if (graph.states.empty())
            return fail("Animator graph has no states.");

        return graph;
        }
        catch (const std::exception& e)
        {
            return fail(std::string {"Animator graph parse error: "} + e.what());
        }
    }

    nlohmann::json graphToJson(const Graph& graph)
    {
        nlohmann::json parameters = nlohmann::json::array();
        for (const auto& p : graph.parameters)
        {
            nlohmann::json pj {{"name", p.name}, {"type", parameterTypeToString(p.type)}};
            if (p.type == ParameterType::eBool)
                pj["default"] = p.defaultBool;
            else
                pj["default"] = p.defaultFloat;
            parameters.push_back(std::move(pj));
        }

        nlohmann::json states = nlohmann::json::array();
        for (const auto& s : graph.states)
        {
            nlohmann::json transitions = nlohmann::json::array();
            for (const auto& t : s.transitions)
                transitions.push_back(transitionToJson(t));
            nlohmann::json sj {{"name", s.name},
                               {"animation", s.animation.toString()},
                               {"speed", s.speed},
                               {"loop", s.loop},
                               {"transitions", std::move(transitions)}};
            if (!s.editor.is_null())
                sj["editor"] = s.editor;
            states.push_back(std::move(sj));
        }

        nlohmann::json anyTransitions = nlohmann::json::array();
        for (const auto& t : graph.anyTransitions)
            anyTransitions.push_back(transitionToJson(t));

        nlohmann::json root {{"version", graph.version},
                             {"name", graph.name},
                             {"entry", graph.entry},
                             {"parameters", std::move(parameters)},
                             {"states", std::move(states)},
                             {"anyTransitions", std::move(anyTransitions)}};
        if (!graph.metadata.is_null())
            root["metadata"] = graph.metadata;
        return root;
    }

    std::optional<Graph> loadGraphFromText(std::string_view text, std::vector<std::string>* diagnostics)
    {
        auto parsed = nlohmann::json::parse(text, nullptr, false);
        if (parsed.is_discarded())
        {
            if (diagnostics)
                diagnostics->push_back("Animator graph JSON failed to parse.");
            return std::nullopt;
        }
        return graphFromJson(parsed, diagnostics);
    }

    std::string saveGraphToText(const Graph& graph) { return graphToJson(graph).dump(2); }

    std::vector<std::string> validateGraph(const Graph& graph)
    {
        std::vector<std::string> diagnostics;
        std::unordered_set<std::string> stateNames;
        for (const auto& s : graph.states)
            if (!stateNames.insert(s.name).second)
                diagnostics.push_back("Duplicate state name: " + s.name);

        if (!graph.entry.empty() && !graph.findState(graph.entry))
            diagnostics.push_back("Entry state not found: " + graph.entry);

        const auto checkTransition = [&](const Transition& t, const std::string& from) {
            if (!graph.findState(t.to))
                diagnostics.push_back("Transition from '" + from + "' targets unknown state '" + t.to + "'.");
            for (const auto& c : t.conditions)
                if (!graph.findParameter(c.parameter))
                    diagnostics.push_back("Condition references unknown parameter '" + c.parameter + "'.");
        };
        for (const auto& s : graph.states)
            for (const auto& t : s.transitions)
                checkTransition(t, s.name);
        for (const auto& t : graph.anyTransitions)
            checkTransition(t, "Any State");

        return diagnostics;
    }
} // namespace vultra::animator_graph
