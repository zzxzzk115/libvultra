#include <vultra/assets/material_graph.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Json                        = nlohmann::json;
        constexpr size_t kMaximumNodes    = 4096;
        constexpr size_t kMaximumEdges    = 65536;
        constexpr size_t kMaximumDocument = 4 * 1024 * 1024;

        bool identifier(std::string_view value)
        {
            const auto letter = [](char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            };
            return !value.empty() && value.size() <= 128 && letter(value.front()) &&
                   std::ranges::all_of(value,
                                       [&](char c)
                                       {
                                           return letter(c) || (c >= '0' && c <= '9');
                                       });
        }

        bool functionName(std::string_view name)
        {
            if (name.size() > 1024)
            {
                return false;
            }
            while (true)
            {
                const auto end = name.find("::");
                if (!identifier(name.substr(0, end)))
                {
                    return false;
                }
                if (end == std::string_view::npos)
                {
                    return true;
                }
                name.remove_prefix(end + 2);
            }
        }

        bool typeName(std::string_view name)
        {
            return !name.empty() && name.size() <= 1024 &&
                   std::ranges::all_of(name,
                                       [](char c)
                                       {
                                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                  (c >= '0' && c <= '9') ||
                                                  std::string_view("_ :<>,[]").find(c) != std::string_view::npos;
                                       });
        }

        bool selection(std::string_view text)
        {
            if (text.size() > 1024)
            {
                return false;
            }
            while (!text.empty())
            {
                if (text.front() == '[')
                {
                    const auto end   = text.find(']');
                    uint32_t   index = 0;
                    if (end == std::string_view::npos)
                    {
                        return false;
                    }
                    const auto converted = std::from_chars(text.data() + 1, text.data() + end, index);
                    if (converted.ec != std::errc {} || converted.ptr != text.data() + end)
                    {
                        return false;
                    }
                    text.remove_prefix(end + 1);
                }
                else
                {
                    const auto end = text.find_first_of(".[");
                    if (!identifier(text.substr(0, end)))
                    {
                        return false;
                    }
                    text.remove_prefix(end == std::string_view::npos ? text.size() : end);
                }
                if (!text.empty() && text.front() == '.')
                {
                    text.remove_prefix(1);
                    if (text.empty() || text.front() == '[')
                    {
                        return false;
                    }
                }
                else if (!text.empty() && text.front() != '[')
                {
                    return false;
                }
            }
            return true;
        }

        std::expected<std::string, std::string> encoded(const Json& value)
        {
            auto text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
            if (Json::parse(text, nullptr, false) != value)
            {
                return std::unexpected("Material graph requires valid UTF-8");
            }
            return text;
        }

        std::string expression(const MaterialGraphValue& value)
        {
            if (value.source != MaterialGraphSource::eLiteral)
            {
                const auto suffix =
                    value.selection.empty() || value.selection.front() == '[' ? value.selection : "." + value.selection;
                return (value.source == MaterialGraphSource::eNode ? "vultraGraph_" : "") + value.name + suffix;
            }
            return std::visit(
                [](auto literal) -> std::string
                {
                    using T = decltype(literal);
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        return literal ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, float>)
                    {
                        std::array<char, 64> buffer;
                        const auto           converted = std::to_chars(buffer.data(),
                                                             buffer.data() + buffer.size(),
                                                             literal,
                                                             std::chars_format::general,
                                                             std::numeric_limits<float>::max_digits10);
                        std::string          number(buffer.data(), converted.ptr);
                        if (number.find_first_of(".eE") == std::string::npos)
                        {
                            number += ".0";
                        }
                        return "float(" + number + ")";
                    }
                    else
                    {
                        return std::string(std::is_same_v<T, int32_t> ? "int(" : "uint(") + std::to_string(literal) +
                               ")";
                    }
                },
                value.literal);
        }

        struct Plan
        {
            std::vector<size_t> order;
            std::vector<bool>   live;
        };

        std::expected<Plan, std::string> plan(const MaterialGraph& graph)
        {
            if (!identifier(graph.entry) || !typeName(graph.resultType) || graph.parameters.size() > 64 ||
                graph.nodes.size() > kMaximumNodes || graph.code.size() > 1024 * 1024 ||
                graph.code.find(char(0)) != std::string::npos)
            {
                return std::unexpected("Invalid or excessive material graph definition");
            }
            std::set<std::string_view> parameters;
            for (const auto& parameter : graph.parameters)
            {
                if (!identifier(parameter.name) || parameter.name.starts_with("vultraGraph_") ||
                    !typeName(parameter.type) || !parameters.insert(parameter.name).second)
                {
                    return std::unexpected("Invalid or duplicate graph parameter: " + parameter.name);
                }
            }
            std::map<std::string_view, size_t> nodes;
            size_t                             edges = 0;
            for (size_t i = 0; i < graph.nodes.size(); ++i)
            {
                const auto& node = graph.nodes[i];
                edges += node.arguments.size();
                if (!identifier(node.id) || !functionName(node.function) || node.arguments.size() > 256 ||
                    edges > kMaximumEdges || !nodes.emplace(node.id, i).second)
                {
                    return std::unexpected("Invalid or duplicate graph node: " + node.id);
                }
            }
            const auto validate = [&](const MaterialGraphValue& value) -> std::expected<void, std::string>
            {
                if (value.source == MaterialGraphSource::eLiteral)
                {
                    if (!value.name.empty() || !value.selection.empty() ||
                        (std::holds_alternative<float>(value.literal) &&
                         !std::isfinite(std::get<float>(value.literal))))
                    {
                        return std::unexpected("Graph literal requires a finite scalar and no reference");
                    }
                }
                else if ((value.source != MaterialGraphSource::eNode &&
                          value.source != MaterialGraphSource::eParameter) ||
                         !selection(value.selection) ||
                         (value.source == MaterialGraphSource::eNode ? !nodes.contains(value.name) :
                                                                       !parameters.contains(value.name)))
                {
                    return std::unexpected("Unknown or invalid graph value: " + value.name);
                }
                return {};
            };
            if (auto valid = validate(graph.result); !valid)
            {
                return std::unexpected(valid.error());
            }
            std::vector<size_t>              incoming(graph.nodes.size());
            std::vector<std::vector<size_t>> dependents(graph.nodes.size());
            for (size_t i = 0; i < graph.nodes.size(); ++i)
            {
                for (const auto& argument : graph.nodes[i].arguments)
                {
                    if (auto valid = validate(argument); !valid)
                    {
                        return std::unexpected("Graph node " + graph.nodes[i].id + ": " + valid.error());
                    }
                    if (argument.source == MaterialGraphSource::eNode)
                    {
                        ++incoming[i];
                        dependents[nodes.find(argument.name)->second].push_back(i);
                    }
                }
            }
            std::set<std::pair<std::string_view, size_t>> ready;
            for (size_t i = 0; i < incoming.size(); ++i)
            {
                if (!incoming[i])
                {
                    ready.emplace(graph.nodes[i].id, i);
                }
            }
            Plan result {{}, std::vector<bool>(graph.nodes.size())};
            while (!ready.empty())
            {
                const auto index = ready.begin()->second;
                ready.erase(ready.begin());
                result.order.push_back(index);
                for (const auto next : dependents[index])
                {
                    if (--incoming[next] == 0)
                    {
                        ready.emplace(graph.nodes[next].id, next);
                    }
                }
            }
            if (result.order.size() != graph.nodes.size())
            {
                return std::unexpected("Material graph contains a cycle");
            }
            std::vector<size_t> pending;
            if (graph.result.source == MaterialGraphSource::eNode)
            {
                pending.push_back(nodes.find(graph.result.name)->second);
            }
            while (!pending.empty())
            {
                const auto index = pending.back();
                pending.pop_back();
                if (result.live[index])
                {
                    continue;
                }
                result.live[index] = true;
                for (const auto& argument : graph.nodes[index].arguments)
                {
                    if (argument.source == MaterialGraphSource::eNode)
                    {
                        pending.push_back(nodes.find(argument.name)->second);
                    }
                }
            }
            return result;
        }

        Json encodeValue(const MaterialGraphValue& value)
        {
            if (value.source != MaterialGraphSource::eLiteral)
            {
                return {{"source", value.source == MaterialGraphSource::eNode ? "node" : "parameter"},
                        {"name", value.name},
                        {"selection", value.selection}};
            }
            return std::visit(
                [](auto literal) -> Json
                {
                    using T = decltype(literal);
                    std::string type;
                    if constexpr (std::is_same_v<T, float>)
                    {
                        type = "float";
                    }
                    if constexpr (std::is_same_v<T, int32_t>)
                    {
                        type = "int";
                    }
                    if constexpr (std::is_same_v<T, uint32_t>)
                    {
                        type = "uint";
                    }
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        type = "bool";
                    }
                    return {{"source", "literal"}, {"type", type}, {"value", literal}};
                },
                value.literal);
        }

        std::expected<MaterialGraphValue, std::string> decodeValue(const Json& value)
        {
            if (!value.is_object() || !value.contains("source") || !value["source"].is_string())
            {
                return std::unexpected("Invalid material graph value");
            }
            MaterialGraphValue result;
            const auto&        source = value["source"].get_ref<const std::string&>();
            if (source == "literal")
            {
                if (value.size() != 3 || !value.contains("type") || !value["type"].is_string() ||
                    !value.contains("value"))
                {
                    return std::unexpected("Invalid graph literal");
                }
                result.source       = MaterialGraphSource::eLiteral;
                const auto& type    = value["type"].get_ref<const std::string&>();
                const auto& literal = value["value"];
                if (type == "float" && literal.is_number() && std::isfinite(literal.get<double>()) &&
                    std::abs(literal.get<double>()) <= std::numeric_limits<float>::max())
                {
                    result.literal = literal.get<float>();
                }
                else if (type == "int" && literal.is_number_integer() &&
                         (!literal.is_number_unsigned() || literal.get<uint64_t>() <= INT32_MAX) &&
                         literal.get<int64_t>() >= INT32_MIN && literal.get<int64_t>() <= INT32_MAX)
                {
                    result.literal = literal.get<int32_t>();
                }
                else if (type == "uint" && literal.is_number_integer() &&
                         (literal.is_number_unsigned() || literal.get<int64_t>() >= 0) &&
                         literal.get<uint64_t>() <= UINT32_MAX)
                {
                    result.literal = literal.get<uint32_t>();
                }
                else if (type == "bool" && literal.is_boolean())
                {
                    result.literal = literal.get<bool>();
                }
                else
                {
                    return std::unexpected("Invalid graph literal type/range");
                }
            }
            else
            {
                if ((source != "node" && source != "parameter") || value.size() != 3 || !value.contains("name") ||
                    !value["name"].is_string() || !value.contains("selection") || !value["selection"].is_string())
                {
                    return std::unexpected("Invalid graph reference");
                }
                result.source    = source == "node" ? MaterialGraphSource::eNode : MaterialGraphSource::eParameter;
                result.name      = value["name"].get_ref<const std::string&>();
                result.selection = value["selection"].get_ref<const std::string&>();
            }
            return result;
        }
    } // namespace

    std::expected<std::string, std::string> MaterialGraph::sourceExpected(const std::filesystem::path& identity) const
    {
        if (identity.generic_string().size() > 4096)
        {
            return std::unexpected("Material graph source identity exceeds 4096 bytes");
        }
        auto ordered = plan(*this);
        if (!ordered)
        {
            return std::unexpected(ordered.error());
        }
        auto label = encoded(identity.generic_string());
        if (!label)
        {
            return std::unexpected(label.error());
        }
        if (identity.generic_string().find(char(0)) != std::string::npos)
        {
            return std::unexpected("Invalid graph source identity");
        }
        if (auto utf8 = encoded(code); !utf8)
        {
            return std::unexpected(utf8.error());
        }
        std::string text = "#line 1 " + *label + "\n" + code + "\n" + resultType + " " + entry + "(";
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            if (i)
            {
                text += ", ";
            }
            text += parameters[i].type + " " + parameters[i].name;
        }
        text += ")\n{\n";
        for (const auto index : ordered->order)
        {
            if (!ordered->live[index])
            {
                continue;
            }
            const auto& node     = nodes[index];
            auto        location = encoded(identity.generic_string() + ":" + node.id);
            if (!location)
            {
                return std::unexpected(location.error());
            }
            text += "#line 1 " + *location + "\n    let vultraGraph_" + node.id + " = " + node.function + "(";
            for (size_t i = 0; i < node.arguments.size(); ++i)
            {
                if (i)
                {
                    text += ", ";
                }
                text += expression(node.arguments[i]);
            }
            text += ");\n";
            if (text.size() > kMaximumDocument)
            {
                return std::unexpected("Generated material graph exceeds source limit");
            }
        }
        text += "    return " + expression(result) + ";\n}\n";
        if (text.size() > kMaximumDocument)
        {
            return std::unexpected("Generated material graph exceeds source limit");
        }
        return text;
    }

    std::expected<std::string, std::string> MaterialGraph::serializeExpected() const
    {
        auto valid = plan(*this);
        if (!valid)
        {
            return std::unexpected(valid.error());
        }
        Json document {{"version", 1},
                       {"kind", "material_graph"},
                       {"entry", entry},
                       {"result_type", resultType},
                       {"parameters", Json::array()},
                       {"code", code},
                       {"nodes", Json::array()},
                       {"result", encodeValue(result)}};
        for (const auto& parameter : parameters)
        {
            document["parameters"].push_back({{"name", parameter.name}, {"type", parameter.type}});
        }
        std::vector<const MaterialGraphNode*> sorted;
        for (const auto& node : nodes)
        {
            sorted.push_back(&node);
        }
        std::ranges::sort(sorted,
                          [](const auto* a, const auto* b)
                          {
                              return a->id < b->id;
                          });
        for (const auto* node : sorted)
        {
            Json arguments = Json::array();
            for (const auto& argument : node->arguments)
            {
                arguments.push_back(encodeValue(argument));
            }
            document["nodes"].push_back(
                {{"id", node->id}, {"function", node->function}, {"arguments", std::move(arguments)}});
        }
        auto text = encoded(document);
        if (!text)
        {
            return text;
        }
        if (text->size() > kMaximumDocument)
        {
            return std::unexpected("Material graph exceeds document limit");
        }
        return text;
    }

    std::expected<MaterialGraph, std::string> MaterialGraph::parseExpected(std::string_view text)
    {
        if (text.size() > kMaximumDocument)
        {
            return std::unexpected("Material graph exceeds document limit");
        }
        bool                               duplicate = false;
        std::vector<std::set<std::string>> objects;
        const auto                         document = Json::parse(
            text,
            [&](int, Json::parse_event_t event, Json& value)
            {
                if (event == Json::parse_event_t::object_start)
                {
                    objects.emplace_back();
                }
                else if (event == Json::parse_event_t::object_end)
                {
                    objects.pop_back();
                }
                else if (event == Json::parse_event_t::key)
                {
                    duplicate |= !objects.back().insert(value.get_ref<const std::string&>()).second;
                }
                return true;
            },
            false);
        if (duplicate || !document.is_object() || document.size() != 8 || !document.contains("version") ||
            !document["version"].is_number_unsigned() || document["version"] != 1 || !document.contains("kind") ||
            document["kind"] != "material_graph" || !document.contains("entry") || !document["entry"].is_string() ||
            !document.contains("result_type") || !document["result_type"].is_string() || !document.contains("code") ||
            !document["code"].is_string() || !document.contains("parameters") || !document["parameters"].is_array() ||
            document["parameters"].size() > 64 || !document.contains("nodes") || !document["nodes"].is_array() ||
            document["nodes"].size() > kMaximumNodes || !document.contains("result"))
        {
            return std::unexpected("Invalid material graph document");
        }
        MaterialGraph graph;
        graph.entry      = document["entry"].get_ref<const std::string&>();
        graph.resultType = document["result_type"].get_ref<const std::string&>();
        graph.code       = document["code"].get_ref<const std::string&>();
        for (const auto& parameter : document["parameters"])
        {
            if (!parameter.is_object() || parameter.size() != 2 || !parameter.contains("name") ||
                !parameter["name"].is_string() || !parameter.contains("type") || !parameter["type"].is_string())
            {
                return std::unexpected("Invalid graph parameter");
            }
            graph.parameters.push_back(
                {parameter["name"].get_ref<const std::string&>(), parameter["type"].get_ref<const std::string&>()});
        }
        for (const auto& node : document["nodes"])
        {
            if (!node.is_object() || node.size() != 3 || !node.contains("id") || !node["id"].is_string() ||
                !node.contains("function") || !node["function"].is_string() || !node.contains("arguments") ||
                !node["arguments"].is_array() || node["arguments"].size() > 256)
            {
                return std::unexpected("Invalid graph node");
            }
            MaterialGraphNode decoded {node["id"].get_ref<const std::string&>(),
                                       node["function"].get_ref<const std::string&>(),
                                       {}};
            for (const auto& argument : node["arguments"])
            {
                auto value = decodeValue(argument);
                if (!value)
                {
                    return std::unexpected(value.error());
                }
                decoded.arguments.push_back(std::move(*value));
            }
            graph.nodes.push_back(std::move(decoded));
        }
        auto result = decodeValue(document["result"]);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        graph.result = std::move(*result);
        if (auto valid = graph.serializeExpected(); !valid)
        {
            return std::unexpected(valid.error());
        }
        return graph;
    }
} // namespace vultra
