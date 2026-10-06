#include <vultra/servers/rendering/builtin/raster_passes.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        void validatePorts(const std::vector<PassPort>& ports, size_t inputCount)
        {
            std::set<std::string> names;
            for (const auto& port : ports)
            {
                if (port.name.empty() || port.name.find('.') != std::string::npos || !names.insert(port.name).second)
                {
                    throw std::invalid_argument("Pass ports need unique nonempty names without dots");
                }
                if (port.kind == PassResourceKind::eBuffer &&
                    (port.format != VriFormat_Unknown || port.sameExtentAsInput))
                {
                    throw std::invalid_argument("Buffer ports cannot declare a texture format or extent");
                }
                if (port.sameExtentAsInput && *port.sameExtentAsInput >= inputCount)
                {
                    throw std::invalid_argument("Pass extent constraint refers to a missing input");
                }
            }
        }

        void validateResource(const RenderGraph&                     graph,
                              const PassPort&                        port,
                              RenderGraph::Resource                  resource,
                              std::span<const RenderGraph::Resource> inputs,
                              std::string_view                       pass)
        {
            const auto info  = graph.resourceInfo(resource);
            const auto label = std::string(pass) + "." + port.name;
            if (info.isTexture != (port.kind == PassResourceKind::eTexture))
            {
                throw std::invalid_argument(label + ": resource type mismatch");
            }
            if (info.isTexture && port.format != VriFormat_Unknown && info.textureDesc.format != port.format)
            {
                throw std::invalid_argument(label + ": texture format mismatch");
            }
            if (port.sameExtentAsInput)
            {
                const auto reference = graph.resourceInfo(inputs[*port.sameExtentAsInput]);
                if (!reference.isTexture || info.textureDesc.width != reference.textureDesc.width ||
                    info.textureDesc.height != reference.textureDesc.height)
                {
                    throw std::invalid_argument(label + ": texture extent mismatch");
                }
            }
        }

        std::vector<double>
        parameterValues(const PassDefinition& definition, std::string_view name, const PassParameters& parameters)
        {
            for (const auto& [key, value] : parameters)
            {
                if (!std::ranges::any_of(definition.parameters,
                                         [&](const auto& parameter)
                                         {
                                             return parameter.name == key;
                                         }))
                {
                    throw std::invalid_argument(std::string(name) + ": unknown parameter: " + key);
                }
            }
            std::vector<double> values;
            values.reserve(definition.parameters.size());
            for (const auto& parameter : definition.parameters)
            {
                const auto found = parameters.find(parameter.name);
                const auto value = found == parameters.end() ? parameter.defaultValue : found->second;
                if (!std::isfinite(value) || value < parameter.minimum || value > parameter.maximum)
                {
                    throw std::invalid_argument(std::string(name) + ": parameter out of range: " + parameter.name);
                }
                values.push_back(value);
            }
            return values;
        }
    } // namespace

    PassCatalog::PassCatalog(Device& device) :
        m_Device(device)
    {
        add(toneMappingDefinition());
        for (auto definition : builtinRasterPassDefinitions())
        {
            add(std::move(definition));
        }
    }

    void PassCatalog::add(PassDefinition definition)
    {
        if (definition.type.empty() || !definition.create)
        {
            throw std::invalid_argument("Pass definition needs a type name and factory");
        }
        if (std::ranges::any_of(m_Definitions,
                                [&](const auto& value)
                                {
                                    return value.type == definition.type;
                                }))
        {
            throw std::invalid_argument("Duplicate pass type: " + definition.type);
        }
        validatePorts(definition.inputs, definition.inputs.size());
        validatePorts(definition.outputs, definition.inputs.size());
        std::set<std::string> names;
        for (const auto& parameter : definition.parameters)
        {
            if (parameter.name.empty() || !names.insert(parameter.name).second ||
                !std::isfinite(parameter.defaultValue) || !std::isfinite(parameter.minimum) ||
                !std::isfinite(parameter.maximum) || parameter.minimum > parameter.defaultValue ||
                parameter.defaultValue > parameter.maximum)
            {
                throw std::invalid_argument("Invalid pass parameter: " + parameter.name);
            }
        }
        m_Definitions.push_back(std::move(definition));
    }

    void PassCatalog::bindFactory(std::string_view type, std::function<std::unique_ptr<GraphPass>(Device&)> create)
    {
        if (!create)
        {
            throw std::invalid_argument("Pass binding requires a factory");
        }
        const auto found = std::ranges::find(m_Definitions, type, &PassDefinition::type);
        if (found == m_Definitions.end())
        {
            throw std::invalid_argument("Unknown bound pass type: " + std::string(type));
        }
        found->create = std::move(create);
    }

    const PassDefinition& PassCatalog::definition(std::string_view type) const
    {
        const auto found = std::ranges::find_if(m_Definitions,
                                                [&](const auto& value)
                                                {
                                                    return value.type == type;
                                                });
        if (found == m_Definitions.end())
        {
            throw std::invalid_argument("Unknown pass type: " + std::string(type));
        }
        return *found;
    }

    BuiltPass PassCatalog::build(RenderGraph&                           graph,
                                 std::string_view                       type,
                                 std::string_view                       name,
                                 std::span<const RenderGraph::Resource> inputs,
                                 const PassParameters&                  parameters) const
    {
        if (&graph.device() != &m_Device)
        {
            throw std::invalid_argument("Pass catalog and graph belong to different devices");
        }
        const auto& desc = definition(type);
        if (name.empty() || inputs.size() != desc.inputs.size())
        {
            throw std::invalid_argument(std::string(name) + ": pass input count mismatch or empty instance name");
        }
        if ((m_Device.core.GetDeviceDesc(m_Device.handle)->enabledFeatures & desc.requiredFeatures) !=
            desc.requiredFeatures)
        {
            throw std::invalid_argument(std::string(name) + ": required VRI features were not enabled");
        }
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            validateResource(graph, desc.inputs[i], inputs[i], inputs, name);
        }
        BuiltPass result;
        result.name            = name;
        result.type            = type;
        result.parameterValues = parameterValues(desc, name, parameters);
        result.instance        = desc.create(m_Device);
        if (!result.instance)
        {
            throw std::runtime_error(std::string(name) + ": pass factory returned no instance");
        }
        result.outputs = result.instance->addPasses(graph, name, inputs, result.parameterValues);
        if (result.outputs.size() != desc.outputs.size())
        {
            throw std::logic_error(std::string(name) + ": pass output count mismatch");
        }
        for (size_t i = 0; i < result.outputs.size(); ++i)
        {
            validateResource(graph, desc.outputs[i], result.outputs[i], inputs, name);
        }
        return result;
    }

    void PassCatalog::setParameters(BuiltPass& pass, const PassParameters& parameters) const
    {
        const auto values = parameterValues(definition(pass.type), pass.name, parameters);
        if (values.size() != pass.parameterValues.size())
        {
            throw std::logic_error(pass.name + ": compiled parameter layout changed");
        }
        std::ranges::copy(values, pass.parameterValues.begin());
    }
} // namespace vultra
