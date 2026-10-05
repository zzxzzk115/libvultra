#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        std::pair<std::string, std::string> endpoint(const std::string& text)
        {
            const auto dot = text.find('.');
            if (dot == 0 || dot == std::string::npos || dot + 1 == text.size() ||
                text.find('.', dot + 1) != std::string::npos)
            {
                throw std::invalid_argument("Graph endpoint needs an instance.port name: " + text);
            }
            return {text.substr(0, dot), text.substr(dot + 1)};
        }

        size_t portIndex(const std::vector<PassPort>& ports, const std::string& name, const std::string& label)
        {
            const auto found = std::ranges::find_if(ports,
                                                    [&](const auto& port)
                                                    {
                                                        return port.name == name;
                                                    });
            if (found == ports.end())
            {
                throw std::invalid_argument("Unknown graph port: " + label);
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

    GraphDefinition GraphDefinition::parse(std::string_view text)
    {
        try
        {
            const auto data = Json::parse(text);
            if (data.at("format") != "vultra.graph" || data.at("version") != 1)
            {
                throw std::invalid_argument("Expected vultra.graph version 1");
            }
            if (!data.at("passes").is_array() || !data.at("edges").is_array() || !data.at("outputs").is_array())
            {
                throw std::invalid_argument("Invalid graph definition: passes, edges and outputs must be arrays");
            }
            GraphDefinition definition;
            for (const auto& pass : data.at("passes"))
            {
                if (!pass.at("parameters").is_object())
                {
                    throw std::invalid_argument("Invalid graph definition: pass parameters must be an object");
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
            throw std::invalid_argument(std::string("Invalid graph definition: ") + error.what());
        }
    }

    void GraphDefinition::save(const std::filesystem::path& file) const
    {
        const auto text = serialize();
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    GraphDefinition GraphDefinition::load(const std::filesystem::path& file)
    {
        std::ifstream source(file, std::ios::binary);
        if (!source)
        {
            throw std::runtime_error("Cannot open graph definition: " + file.string());
        }
        const std::string text {std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
        return parse(text);
    }

    GraphBuild
    GraphDefinition::build(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const
    {
        if (outputs.empty())
        {
            throw std::invalid_argument("Graph definition needs at least one output");
        }
        std::map<std::string, size_t>                indices;
        std::map<std::string, RenderGraph::Resource> resources;
        for (const auto& input : imports)
        {
            endpoint(input.name);
            graph.resourceInfo(input.resource);
            if (!resources.emplace(input.name, input.resource).second)
            {
                throw std::invalid_argument("Duplicate graph import: " + input.name);
            }
        }
        for (size_t i = 0; i < passes.size(); ++i)
        {
            const auto& pass = passes[i];
            if (pass.id.empty() || pass.id.find('.') != std::string::npos || !indices.emplace(pass.id, i).second)
            {
                throw std::invalid_argument("Invalid or duplicate pass instance: " + pass.id);
            }
            catalog.definition(pass.type);
            for (const auto& input : imports)
            {
                if (endpoint(input.name).first == pass.id)
                {
                    throw std::invalid_argument("Pass instance conflicts with graph import: " + pass.id);
                }
            }
        }
        std::vector<std::vector<std::string>> sources(passes.size());
        std::vector<std::set<size_t>>         dependencies(passes.size());
        for (size_t i = 0; i < passes.size(); ++i)
        {
            sources[i].resize(catalog.definition(passes[i].type).inputs.size());
        }
        const auto validateSource = [&](const std::string& name) -> std::optional<size_t>
        {
            if (resources.contains(name))
            {
                return std::nullopt;
            }
            const auto [id, port] = endpoint(name);
            const auto found      = indices.find(id);
            if (found == indices.end())
            {
                throw std::invalid_argument("Unknown graph source: " + name);
            }
            portIndex(catalog.definition(passes[found->second].type).outputs, port, name);
            return found->second;
        };
        for (const auto& edge : edges)
        {
            const auto [id, port] = endpoint(edge.to);
            const auto found      = indices.find(id);
            if (found == indices.end())
            {
                throw std::invalid_argument("Unknown graph destination: " + edge.to);
            }
            const auto index = found->second;
            const auto input = portIndex(catalog.definition(passes[index].type).inputs, port, edge.to);
            if (!sources[index][input].empty())
            {
                throw std::invalid_argument("Graph input connected more than once: " + edge.to);
            }
            sources[index][input] = edge.from;
            if (const auto dependency = validateSource(edge.from))
            {
                dependencies[index].insert(*dependency);
            }
        }
        for (size_t i = 0; i < passes.size(); ++i)
        {
            const auto& desc = catalog.definition(passes[i].type);
            for (size_t input = 0; input < sources[i].size(); ++input)
            {
                if (sources[i][input].empty())
                {
                    throw std::invalid_argument("Unconnected graph input: " + passes[i].id + "." +
                                                desc.inputs[input].name);
                }
            }
        }
        std::set<std::string> outputNames;
        for (const auto& output : outputs)
        {
            validateSource(output);
            if (!outputNames.insert(output).second)
            {
                throw std::invalid_argument("Duplicate graph output: " + output);
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
                throw std::invalid_argument("Graph definition contains a dependency cycle");
            }
        }
        GraphBuild built;
        for (const auto index : order)
        {
            const auto&                        pass = passes[index];
            const auto&                        desc = catalog.definition(pass.type);
            std::vector<RenderGraph::Resource> inputs;
            for (const auto& source : sources[index])
            {
                inputs.push_back(resources.at(source));
            }
            auto instance = catalog.build(graph, pass.type, pass.id, inputs, pass.parameters);
            for (size_t i = 0; i < desc.outputs.size(); ++i)
            {
                resources.emplace(pass.id + "." + desc.outputs[i].name, instance.outputs[i]);
            }
            built.passes.push_back(std::move(instance));
        }
        for (const auto& name : outputs)
        {
            const auto resource = resources.at(name);
            graph.exportResource(resource);
            built.outputs.push_back({name, resource});
        }
        return built;
    }
} // namespace vultra
