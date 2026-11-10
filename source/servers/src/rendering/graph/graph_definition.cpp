#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr size_t maximumGraphBytes = 64 * 1024 * 1024;

        std::expected<std::pair<std::string, std::string>, std::string> endpoint(const std::string& text)
        {
            const auto dot = text.find('.');
            if (dot == 0 || dot == std::string::npos || dot + 1 == text.size() ||
                text.find('.', dot + 1) != std::string::npos)
            {
                return std::unexpected("Graph endpoint needs an instance.port name: " + text);
            }
            return std::pair {text.substr(0, dot), text.substr(dot + 1)};
        }

        std::expected<size_t, std::string>
        portIndex(const std::vector<PassPort>& ports, const std::string& name, const std::string& label)
        {
            const auto found = std::ranges::find(ports, name, &PassPort::name);
            if (found == ports.end())
            {
                return std::unexpected("Unknown graph port: " + label);
            }
            return size_t(found - ports.begin());
        }
    } // namespace

    std::expected<std::string, std::string> GraphDefinition::serializeExpected() const
    {
        Json data {{"format", "vultra.graph"},
                   {"version", 1},
                   {"passes", Json::array()},
                   {"edges", Json::array()},
                   {"outputs", outputs}};
        for (const auto& pass : passes)
        {
            for (const auto& [name, value] : pass.parameters)
            {
                if (!std::isfinite(value))
                {
                    return std::unexpected("Graph parameter must be finite: " + pass.id + "." + name);
                }
            }
            data["passes"].push_back({{"id", pass.id}, {"type", pass.type}, {"parameters", pass.parameters}});
        }
        for (const auto& edge : edges)
        {
            data["edges"].push_back({{"from", edge.from}, {"to", edge.to}});
        }
        // Check the round trip before publication: replacement must never change persistent names or values.
        auto text = data.dump(2, ' ', false, Json::error_handler_t::replace) + '\n';
        if (text.size() > maximumGraphBytes || Json::parse(text, nullptr, false) != data)
        {
            return std::unexpected("Graph persistence requires valid UTF-8 and at most 64 MiB");
        }
        return text;
    }

    std::expected<GraphDefinition, std::string> GraphDefinition::parseExpected(std::string_view source)
    {
        if (source.size() > maximumGraphBytes)
        {
            return std::unexpected("Graph definition exceeds 64 MiB");
        }
        using Json      = nlohmann::json;
        const auto data = Json::parse(source.begin(), source.end(), nullptr, false);
        if (data.is_discarded() || !data.is_object())
            return std::unexpected("Invalid graph JSON");
        const auto format  = data.find("format");
        const auto version = data.find("version");
        const auto passes  = data.find("passes");
        const auto edges   = data.find("edges");
        const auto outputs = data.find("outputs");
        if (format == data.end() || !format->is_string() || *format != "vultra.graph" || version == data.end() ||
            !version->is_number_integer() || *version != 1 || passes == data.end() || !passes->is_array() ||
            edges == data.end() || !edges->is_array() || outputs == data.end() || !outputs->is_array())
            return std::unexpected("Expected vultra.graph version 1 with pass, edge and output arrays");
        GraphDefinition graph;
        for (const auto& pass : *passes)
        {
            if (!pass.is_object())
                return std::unexpected("Graph pass must be an object");
            const auto id         = pass.find("id");
            const auto type       = pass.find("type");
            const auto parameters = pass.find("parameters");
            if (id == pass.end() || !id->is_string() || type == pass.end() || !type->is_string() ||
                parameters == pass.end() || !parameters->is_object())
                return std::unexpected("Graph pass requires id, type and parameter object");
            GraphPassDesc value;
            value.id   = id->get<std::string>();
            value.type = type->get<std::string>();
            for (auto it = parameters->begin(); it != parameters->end(); ++it)
            {
                if (!it.value().is_number())
                    return std::unexpected("Graph parameter must be numeric: " + it.key());
                const auto number = it.value().get<double>();
                if (!std::isfinite(number))
                    return std::unexpected("Graph parameter must be finite: " + it.key());
                value.parameters.emplace(it.key(), number);
            }
            graph.passes.push_back(std::move(value));
        }
        for (const auto& edge : *edges)
        {
            if (!edge.is_object())
                return std::unexpected("Graph edge must be an object");
            const auto from = edge.find("from");
            const auto to   = edge.find("to");
            if (from == edge.end() || !from->is_string() || to == edge.end() || !to->is_string())
                return std::unexpected("Graph edge requires string from and to endpoints");
            graph.edges.push_back({from->get<std::string>(), to->get<std::string>()});
        }
        for (const auto& output : *outputs)
        {
            if (!output.is_string())
                return std::unexpected("Graph output must be a string");
            graph.outputs.push_back(output.get<std::string>());
        }
        return graph;
    }

    std::expected<void, std::string> GraphDefinition::saveExpected(const std::filesystem::path& file) const
    {
        if (file.empty() || file.native().find(std::filesystem::path::value_type {}) != std::string::npos)
        {
            return std::unexpected("Graph definition requires a nonempty path without embedded nulls");
        }
        auto text = serializeExpected();
        if (!text)
        {
            return std::unexpected(text.error());
        }
        return writeFileAtomicallyExpected(file, std::as_bytes(std::span(*text)));
    }

    std::expected<GraphDefinition, std::string> GraphDefinition::loadExpected(const std::filesystem::path& file)
    {
        if (file.empty() || file.native().find(std::filesystem::path::value_type {}) != std::string::npos)
        {
            return std::unexpected("Graph definition requires a nonempty path without embedded nulls");
        }
        std::ifstream source(file, std::ios::binary);
        if (!source)
        {
            return std::unexpected("Cannot open graph definition: " + file.string());
        }
        std::string                 text;
        std::array<char, 64 * 1024> buffer {};
        while (source.read(buffer.data(), std::streamsize(buffer.size())) || source.gcount())
        {
            const auto count = size_t(source.gcount());
            if (count > maximumGraphBytes - text.size())
            {
                return std::unexpected("Graph definition exceeds 64 MiB: " + file.string());
            }
            text.append(buffer.data(), count);
        }
        if (source.bad() || !source.eof())
        {
            return std::unexpected("Read graph definition failed: " + file.string());
        }
        auto graph = parseExpected(text);
        if (!graph)
        {
            return std::unexpected("Parse graph definition " + file.string() + ": " + graph.error());
        }
        return graph;
    }

    GraphBuild
    GraphDefinition::build(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const
    {
        auto built = buildExpected(graph, catalog, imports);
        if (!built)
        {
            throw std::invalid_argument(built.error());
        }
        return std::move(*built);
    }

    std::expected<GraphBuild, std::string> GraphDefinition::buildExpected(RenderGraph&                  graph,
                                                                          const PassCatalog&            catalog,
                                                                          std::span<const GraphBinding> imports) const
    {
        if (outputs.empty())
        {
            return std::unexpected("Graph definition needs at least one output");
        }
        if (auto editable = graph.checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        std::map<std::string, size_t>                indices;
        std::map<std::string, RenderGraph::Resource> resources;
        std::set<std::string>                        importInstances;
        for (const auto& input : imports)
        {
            auto location = endpoint(input.name);
            if (!location)
            {
                return std::unexpected(location.error());
            }
            if (!graph.ownsResource(input.resource))
            {
                return std::unexpected(input.name + ": resource belongs to another graph");
            }
            if (!resources.emplace(input.name, input.resource).second)
            {
                return std::unexpected("Duplicate graph import: " + input.name);
            }
            importInstances.insert(location->first);
        }
        std::vector<const PassDefinition*> definitions;
        definitions.reserve(passes.size());
        for (size_t i = 0; i < passes.size(); ++i)
        {
            const auto& pass = passes[i];
            if (pass.id.empty() || pass.id.find('.') != std::string::npos || !indices.emplace(pass.id, i).second)
            {
                return std::unexpected("Invalid or duplicate pass instance: " + pass.id);
            }
            auto definition = catalog.definitionExpected(pass.type);
            if (!definition)
            {
                return std::unexpected(definition.error());
            }
            auto values = (*definition)->parameterValuesExpected(pass.id, pass.parameters);
            if (!values)
            {
                return std::unexpected(values.error());
            }
            if (importInstances.contains(pass.id))
            {
                return std::unexpected("Pass instance conflicts with graph import: " + pass.id);
            }
            definitions.push_back(*definition);
        }
        std::vector<std::vector<std::string>> sources(passes.size());
        std::vector<std::set<size_t>>         dependencies(passes.size());
        for (size_t i = 0; i < passes.size(); ++i)
        {
            sources[i].resize(definitions[i]->inputs.size());
        }
        const auto validateSource = [&](const std::string& name) -> std::expected<std::optional<size_t>, std::string>
        {
            if (resources.contains(name))
            {
                return std::optional<size_t> {};
            }
            auto location = endpoint(name);
            if (!location)
            {
                return std::unexpected(location.error());
            }
            const auto found = indices.find(location->first);
            if (found == indices.end())
            {
                return std::unexpected("Unknown graph source: " + name);
            }
            if (auto port = portIndex(definitions[found->second]->outputs, location->second, name); !port)
            {
                return std::unexpected(port.error());
            }
            return std::optional<size_t> {found->second};
        };
        for (const auto& edge : edges)
        {
            auto location = endpoint(edge.to);
            if (!location)
            {
                return std::unexpected(location.error());
            }
            const auto found = indices.find(location->first);
            if (found == indices.end())
            {
                return std::unexpected("Unknown graph destination: " + edge.to);
            }
            const auto index = found->second;
            auto       input = portIndex(definitions[index]->inputs, location->second, edge.to);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            if (!sources[index][*input].empty())
            {
                return std::unexpected("Graph input connected more than once: " + edge.to);
            }
            sources[index][*input] = edge.from;
            auto dependency        = validateSource(edge.from);
            if (!dependency)
            {
                return std::unexpected(dependency.error());
            }
            if (*dependency)
            {
                dependencies[index].insert(**dependency);
            }
        }
        for (size_t i = 0; i < passes.size(); ++i)
        {
            for (size_t input = 0; input < sources[i].size(); ++input)
            {
                if (sources[i][input].empty())
                {
                    return std::unexpected("Unconnected graph input: " + passes[i].id + "." +
                                           definitions[i]->inputs[input].name);
                }
            }
        }
        std::vector<bool>   live(passes.size());
        std::vector<size_t> pending;
        for (size_t i = 0; i < passes.size(); ++i)
        {
            if (definitions[i]->sideEffect)
            {
                pending.push_back(i);
            }
        }
        std::set<std::string> outputNames;
        for (const auto& output : outputs)
        {
            auto producer = validateSource(output);
            if (!producer)
            {
                return std::unexpected(producer.error());
            }
            if (*producer)
            {
                pending.push_back(**producer);
            }
            if (!outputNames.insert(output).second)
            {
                return std::unexpected("Duplicate graph output: " + output);
            }
        }
        while (!pending.empty())
        {
            const auto index = pending.back();
            pending.pop_back();
            if (!live[index])
            {
                live[index] = true;
                pending.insert(pending.end(), dependencies[index].begin(), dependencies[index].end());
            }
        }
        // Retain file order for independent passes, resolving dependencies before consumers.
        std::vector<size_t> order;
        std::vector<bool>   visited(passes.size());
        while (order.size() < passes.size())
        {
            const auto before = order.size();
            for (size_t i = 0; i < passes.size(); ++i)
            {
                if (!visited[i] && std::ranges::all_of(dependencies[i],
                                                       [&](size_t index)
                                                       {
                                                           return visited[index];
                                                       }))
                {
                    visited[i] = true;
                    order.push_back(i);
                }
            }
            if (order.size() == before)
            {
                return std::unexpected("Graph definition contains a dependency cycle");
            }
        }
        GraphBuild built;
        // The transaction dies first on rejection, before any successful preceding instance in built.
        RenderGraph::AppendTransaction transaction(graph);
        for (const auto index : order)
        {
            if (!live[index])
            {
                continue;
            }
            const auto&                        pass = passes[index];
            std::vector<RenderGraph::Resource> inputs;
            for (const auto& source : sources[index])
            {
                inputs.push_back(resources.find(source)->second);
            }
            auto instance = catalog.buildExpected(graph, pass.type, pass.id, inputs, pass.parameters);
            if (!instance)
            {
                return std::unexpected(instance.error());
            }
            for (size_t i = 0; i < definitions[index]->outputs.size(); ++i)
            {
                resources.emplace(pass.id + "." + definitions[index]->outputs[i].name, instance->outputs[i]);
            }
            built.passes.push_back(std::move(*instance));
        }
        for (const auto& name : outputs)
        {
            const auto resource = resources.find(name)->second;
            if (auto exported = graph.exportResourceExpected(resource); !exported)
            {
                return std::unexpected(exported.error());
            }
            built.outputs.push_back({name, resource});
        }
        transaction.commit();
        return built;
    }
} // namespace vultra
