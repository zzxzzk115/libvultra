#include <vultra/graph/pass_catalog.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace vultra
{
    namespace
    {
        std::expected<void, Error> validatePorts(const std::vector<PassPort>& ports, size_t inputCount)
        {
            std::set<std::string> names;
            for (const auto& port : ports)
            {
                if (port.name.empty() || port.name.find('.') != std::string::npos || !names.insert(port.name).second)
                {
                    return makeError("Pass ports need unique nonempty names without dots");
                }
                if (port.kind == PassResourceKind::eBuffer &&
                    (port.format != VriFormat_Unknown || port.sameExtentAsInput))
                {
                    return makeError("Buffer ports cannot declare a texture format or extent");
                }
                if (port.sameExtentAsInput && *port.sameExtentAsInput >= inputCount)
                {
                    return makeError("Pass extent constraint refers to a missing input");
                }
            }
            return {};
        }

        std::expected<void, Error> validateResource(const RenderGraph&                     graph,
                                                    const PassPort&                        port,
                                                    RenderGraph::Resource                  resource,
                                                    std::span<const RenderGraph::Resource> inputs,
                                                    std::string_view                       pass)
        {
            const auto info = graph.resourceInfo(resource);
            if (!info)
            {
                return std::unexpected(info.error());
            }
            const auto label = std::format("{}.{}", pass, port.name);
            if (info->isTexture != (port.kind == PassResourceKind::eTexture))
            {
                return makeError("{}: resource type mismatch", label);
            }
            if (info->isTexture && port.format != VriFormat_Unknown && info->textureDesc.format != port.format)
            {
                return makeError("{}: texture format mismatch", label);
            }
            if (port.sameExtentAsInput)
            {
                const auto reference = graph.resourceInfo(inputs[*port.sameExtentAsInput]);
                if (!reference)
                {
                    return std::unexpected(reference.error());
                }
                if (!reference->isTexture || info->textureDesc.width != reference->textureDesc.width ||
                    info->textureDesc.height != reference->textureDesc.height)
                {
                    return makeError("{}: texture extent mismatch", label);
                }
            }
            return {};
        }
    } // namespace

    std::expected<std::vector<double>, Error> PassDefinition::parameterValues(std::string_view      instance,
                                                                              const PassParameters& supplied) const
    {
        for (const auto& [key, value] : supplied)
        {
            if (!std::ranges::any_of(parameters,
                                     [&](const auto& parameter)
                                     {
                                         return parameter.name == key;
                                     }))
            {
                return makeError("{}: unknown parameter: {}", instance, key);
            }
        }
        std::vector<double> values;
        values.reserve(parameters.size());
        for (const auto& parameter : parameters)
        {
            const auto found = supplied.find(parameter.name);
            const auto value = found == supplied.end() ? parameter.defaultValue : found->second;
            if (!std::isfinite(value) || value < parameter.minimum || value > parameter.maximum)
            {
                return makeError("{}: parameter out of range: {}", instance, parameter.name);
            }
            if ((parameter.control == PassControl::eCheckbox && value != 0 && value != 1) ||
                (!parameter.choices.empty() && !std::ranges::any_of(parameter.choices,
                                                                    [&](const auto& choice)
                                                                    {
                                                                        return choice.value == value;
                                                                    })))
            {
                return makeError("{}: invalid discrete parameter: {}", instance, parameter.name);
            }
            values.push_back(value);
        }
        return values;
    }

    PassCatalog::PassCatalog(Device& device) :
        m_Device(device)
    {
    }

    std::expected<void, Error> PassCatalog::add(PassDefinition definition)
    {
        if (definition.type.empty() || !definition.create)
        {
            return makeError("Pass definition needs a type name and factory");
        }
        if (std::ranges::any_of(m_Definitions,
                                [&](const auto& value)
                                {
                                    return value.type == definition.type;
                                }))
        {
            return makeError("Duplicate pass type: {}", definition.type);
        }
        if (auto valid = validatePorts(definition.inputs, definition.inputs.size()); !valid)
        {
            return valid;
        }
        if (auto valid = validatePorts(definition.outputs, definition.inputs.size()); !valid)
        {
            return valid;
        }
        std::set<std::string> names;
        for (const auto& parameter : definition.parameters)
        {
            if (parameter.name.empty() || !names.insert(parameter.name).second ||
                !std::isfinite(parameter.defaultValue) || !std::isfinite(parameter.minimum) ||
                !std::isfinite(parameter.maximum) || parameter.minimum > parameter.defaultValue ||
                parameter.defaultValue > parameter.maximum)
            {
                return makeError("Invalid pass parameter: {}", parameter.name);
            }
            if (parameter.control < PassControl::eSlider || parameter.control > PassControl::eReadOnly ||
                (parameter.control == PassControl::eChoice && parameter.choices.empty()) ||
                (parameter.control == PassControl::eCheckbox &&
                 (parameter.minimum != 0 || parameter.maximum != 1 ||
                  (parameter.defaultValue != 0 && parameter.defaultValue != 1))))
            {
                return makeError("Invalid pass parameter control: {}", parameter.name);
            }
            std::set<double> choiceValues;
            for (const auto& choice : parameter.choices)
            {
                if (choice.label.empty() || !std::isfinite(choice.value) || choice.value < parameter.minimum ||
                    choice.value > parameter.maximum || !choiceValues.insert(choice.value).second)
                {
                    return makeError("Invalid pass parameter choice: {}", parameter.name);
                }
            }
        }
        if (auto defaults = definition.parameterValues(definition.type); !defaults)
        {
            return std::unexpected(defaults.error());
        }
        m_Definitions.push_back(std::move(definition));
        return {};
    }

    std::expected<void, Error> PassCatalog::bindFactory(std::string_view type, GraphPassFactory create)
    {
        if (!create)
        {
            return makeError("Pass binding requires a factory");
        }
        const auto found = std::ranges::find(m_Definitions, type, &PassDefinition::type);
        if (found == m_Definitions.end())
        {
            return makeError("Unknown bound pass type: {}", type);
        }
        found->create = std::move(create);
        return {};
    }

    std::expected<const PassDefinition*, Error> PassCatalog::definition(std::string_view type) const
    {
        const auto found = std::ranges::find_if(m_Definitions,
                                                [&](const auto& value)
                                                {
                                                    return value.type == type;
                                                });
        if (found == m_Definitions.end())
        {
            return makeError("Unknown pass type: {}", type);
        }
        return &*found;
    }

    std::expected<BuiltPass, Error> PassCatalog::build(RenderGraph&                           graph,
                                                       std::string_view                       type,
                                                       std::string_view                       name,
                                                       std::span<const RenderGraph::Resource> inputs,
                                                       const PassParameters&                  parameters) const
    {
        if (&graph.device() != &m_Device)
        {
            return makeError("Pass catalog and graph belong to different devices");
        }
        const auto found = definition(type);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        const auto& desc = **found;
        if (name.empty() || inputs.size() != desc.inputs.size())
        {
            return makeError("{}: pass input count mismatch or empty instance name", name);
        }
        if ((m_Device.core.GetDeviceDesc(m_Device.handle)->enabledFeatures & desc.requiredFeatures) !=
            desc.requiredFeatures)
        {
            return makeError("{}: required VRI features were not enabled", name);
        }
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (auto valid = validateResource(graph, desc.inputs[i], inputs[i], inputs, name); !valid)
            {
                return std::unexpected(valid.error());
            }
        }
        auto values = desc.parameterValues(name, parameters);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        auto instance = desc.create(m_Device);
        if (!instance)
        {
            return withContext(name, instance.error());
        }
        if (!*instance)
        {
            return makeError("{}: pass factory returned no instance", name);
        }
        BuiltPass result;
        result.name            = name;
        result.type            = type;
        result.parameterValues = std::move(*values);
        result.instance        = std::move(*instance);
        const auto firstPass   = graph.m_Passes.size();
        auto       outputs     = result.instance->addPasses(graph, name, inputs, result.parameterValues);
        if (!outputs)
        {
            return std::unexpected(outputs.error());
        }
        result.outputs = std::move(*outputs);
        for (size_t i = firstPass; i < graph.m_Passes.size(); ++i)
        {
            if (!desc.sideEffect && graph.m_Passes[i].sideEffect)
            {
                return makeError("{}: side effects must be declared in PassDefinition", name);
            }
            // A catalog instance may expand to several command passes, all owned by this construction scope.
            graph.m_Passes[i].sideEffect = desc.sideEffect;
        }
        if (result.outputs.size() != desc.outputs.size())
        {
            return makeError("{}: pass output count mismatch", name);
        }
        for (size_t i = 0; i < result.outputs.size(); ++i)
        {
            if (auto valid = validateResource(graph, desc.outputs[i], result.outputs[i], inputs, name); !valid)
            {
                return std::unexpected(valid.error());
            }
        }
        return result;
    }

    std::expected<void, Error> PassCatalog::setParameters(BuiltPass& pass, const PassParameters& parameters) const
    {
        const auto found = definition(pass.type);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        const auto values = (*found)->parameterValues(pass.name, parameters);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        if (values->size() != pass.parameterValues.size())
        {
            return makeError("{}: compiled parameter layout changed", pass.name);
        }
        std::ranges::copy(*values, pass.parameterValues.begin());
        return {};
    }
} // namespace vultra
