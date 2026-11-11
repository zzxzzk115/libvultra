#include <vultra/graph/graph_definition.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        std::expected<std::pair<std::string, std::string>, Error> endpoint(const std::string& text)
        {
            const auto dot = text.find('.');
            if (dot == 0 || dot == std::string::npos || dot + 1 == text.size() ||
                text.find('.', dot + 1) != std::string::npos)
            {
                return makeError("Graph endpoint needs an instance.port name: {}", text);
            }
            return std::pair {text.substr(0, dot), text.substr(dot + 1)};
        }

        std::expected<size_t, Error>
        portIndex(const std::vector<PassPort>& ports, const std::string& name, const std::string& label)
        {
            const auto found = std::ranges::find_if(ports,
                                                    [&](const auto& port)
                                                    {
                                                        return port.name == name;
                                                    });
            if (found == ports.end())
            {
                return makeError("Unknown graph port: {}", label);
            }
            return size_t(found - ports.begin());
        }
    } // namespace

    std::string GraphDefinition::serialize() const
    {
        Json data {{"format", "vultra.graph"},
                   {"version", 1},
                   {"passes", Json::array()},
                   {"edges", Json::array()},
                   {"outputs", outputs}};
        for (const auto& pass : passes)
        {
            data["passes"].push_back({{"id", pass.id}, {"type", pass.type}, {"parameters", pass.parameters}});
        }
        for (const auto& edge : edges)
        {
            data["edges"].push_back({{"from", edge.from}, {"to", edge.to}});
        }
        return data.dump(2) + '\n';
    }

    std::expected<GraphDefinition, Error> GraphDefinition::parse(std::string_view text)
    {
        const auto data = Json::parse(text, nullptr, false);
        if (data.is_discarded())
        {
            return makeError("Invalid graph definition: malformed JSON");
        }
        // nlohmann reports missing keys and mistyped values from at() and get<>() only by throwing.
        try
        {
            if (data.at("format") != "vultra.graph" || data.at("version") != 1)
            {
                return makeError("Expected vultra.graph version 1");
            }
            if (!data.at("passes").is_array() || !data.at("edges").is_array() || !data.at("outputs").is_array())
            {
                return makeError("Invalid graph definition: passes, edges and outputs must be arrays");
            }
            GraphDefinition definition;
            for (const auto& pass : data.at("passes"))
            {
                if (!pass.at("parameters").is_object())
                {
                    return makeError("Invalid graph definition: pass parameters must be an object");
                }
                definition.passes.push_back({pass.at("id").get<std::string>(),
                                             pass.at("type").get<std::string>(),
                                             pass.at("parameters").get<PassParameters>()});
            }
            for (const auto& edge : data.at("edges"))
            {
                definition.edges.push_back({edge.at("from").get<std::string>(), edge.at("to").get<std::string>()});
            }
            definition.outputs = data.at("outputs").get<std::vector<std::string>>();
            return definition;
        }
        catch (const Json::exception& error)
        {
            return makeError("Invalid graph definition: {}", error.what());
        }
    }

    std::expected<void, Error> GraphDefinition::save(const std::filesystem::path& file) const
    {
        const auto text = serialize();
        return writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    std::expected<GraphDefinition, Error> GraphDefinition::load(const std::filesystem::path& file)
    {
        std::ifstream source(file, std::ios::binary);
        if (!source)
        {
            return makeError("Cannot open graph definition: {}", file.string());
        }
        const std::string text {std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
        return parse(text);
    }

    std::expected<GraphBuild, Error>
    GraphDefinition::build(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const
    {
        if (outputs.empty())
        {
            return makeError("Graph definition needs at least one output");
        }
        std::map<std::string, size_t>                indices;
        std::map<std::string, RenderGraph::Resource> resources;
        for (const auto& input : imports)
        {
            if (auto valid = endpoint(input.name); !valid)
            {
                return std::unexpected(valid.error());
            }
            if (auto info = graph.resourceInfo(input.resource); !info)
            {
                return std::unexpected(info.error());
            }
            if (!resources.emplace(input.name, input.resource).second)
            {
                return makeError("Duplicate graph import: {}", input.name);
            }
        }
        // Resolved once; the catalog is not modified while this definition builds.
        std::vector<const PassDefinition*> definitions(passes.size());
        for (size_t i = 0; i < passes.size(); ++i)
        {
            const auto& pass = passes[i];
            if (pass.id.empty() || pass.id.find('.') != std::string::npos || !indices.emplace(pass.id, i).second)
            {
                return makeError("Invalid or duplicate pass instance: {}", pass.id);
            }
            const auto found = catalog.definition(pass.type);
            if (!found)
            {
                return std::unexpected(found.error());
            }
            definitions[i] = *found;
            if (auto values = definitions[i]->parameterValues(pass.id, pass.parameters); !values)
            {
                return std::unexpected(values.error());
            }
            for (const auto& input : imports)
            {
                // Import names were validated above.
                if (endpoint(input.name)->first == pass.id)
                {
                    return makeError("Pass instance conflicts with graph import: {}", pass.id);
                }
            }
        }
        std::vector<std::vector<std::string>> sources(passes.size());
        std::vector<std::set<size_t>>         dependencies(passes.size());
        for (size_t i = 0; i < passes.size(); ++i)
        {
            sources[i].resize(definitions[i]->inputs.size());
        }
        const auto validateSource = [&](const std::string& name) -> std::expected<std::optional<size_t>, Error>
        {
            if (resources.contains(name))
            {
                return std::nullopt;
            }
            const auto parts = endpoint(name);
            if (!parts)
            {
                return std::unexpected(parts.error());
            }
            const auto& [id, port] = *parts;
            const auto found       = indices.find(id);
            if (found == indices.end())
            {
                return makeError("Unknown graph source: {}", name);
            }
            if (auto output = portIndex(definitions[found->second]->outputs, port, name); !output)
            {
                return std::unexpected(output.error());
            }
            return found->second;
        };
        for (const auto& edge : edges)
        {
            const auto parts = endpoint(edge.to);
            if (!parts)
            {
                return std::unexpected(parts.error());
            }
            const auto& [id, port] = *parts;
            const auto found       = indices.find(id);
            if (found == indices.end())
            {
                return makeError("Unknown graph destination: {}", edge.to);
            }
            const auto index = found->second;
            const auto input = portIndex(definitions[index]->inputs, port, edge.to);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            if (!sources[index][*input].empty())
            {
                return makeError("Graph input connected more than once: {}", edge.to);
            }
            sources[index][*input] = edge.from;
            const auto dependency  = validateSource(edge.from);
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
                    return makeError("Unconnected graph input: {}.{}",
                                     passes[i].id,
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
            const auto producer = validateSource(output);
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
                return makeError("Duplicate graph output: {}", output);
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
        // Keep file order for independent passes, while resolving explicit dependencies first.
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
                return makeError("Graph definition contains a dependency cycle");
            }
        }
        GraphBuild built;
        for (const auto index : order)
        {
            if (!live[index])
            {
                continue;
            }
            const auto&                        pass = passes[index];
            const auto&                        desc = *definitions[index];
            std::vector<RenderGraph::Resource> inputs;
            for (const auto& source : sources[index])
            {
                inputs.push_back(resources.at(source));
            }
            auto instance = catalog.build(graph, pass.type, pass.id, inputs, pass.parameters);
            if (!instance)
            {
                return std::unexpected(instance.error());
            }
            for (size_t i = 0; i < desc.outputs.size(); ++i)
            {
                resources.emplace(pass.id + "." + desc.outputs[i].name, instance->outputs[i]);
            }
            built.passes.push_back(std::move(*instance));
        }
        for (const auto& name : outputs)
        {
            const auto resource = resources.at(name);
            if (auto exported = graph.exportResource(resource); !exported)
            {
                return std::unexpected(exported.error());
            }
            built.outputs.push_back({name, resource});
        }
        return built;
    }
} // namespace vultra
