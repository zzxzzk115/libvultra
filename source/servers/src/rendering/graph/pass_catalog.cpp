#include <vultra/servers/rendering/graph/pass_catalog.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::expected<void, std::string> validatePorts(const std::vector<PassPort>& ports, size_t inputCount)
        {
            std::set<std::string> names;
            for (const auto& port : ports)
            {
                if (port.name.empty() || port.name.find('.') != std::string::npos || !names.insert(port.name).second)
                {
                    return std::unexpected("Pass ports need unique nonempty names without dots");
                }
                if (port.kind != PassResourceKind::eTexture && port.kind != PassResourceKind::eBuffer)
                {
                    return std::unexpected("Unknown pass port resource kind");
                }
                if (port.kind == PassResourceKind::eBuffer &&
                    (port.format != VriFormat_Unknown || port.sameExtentAsInput))
                {
                    return std::unexpected("Buffer ports cannot declare a texture format or extent");
                }
                if (port.sameExtentAsInput && *port.sameExtentAsInput >= inputCount)
                {
                    return std::unexpected("Pass extent constraint refers to a missing input");
                }
            }
            return {};
        }

        std::expected<void, std::string> validateResource(const RenderGraph&                     graph,
                                                          const PassPort&                        port,
                                                          RenderGraph::Resource                  resource,
                                                          std::span<const RenderGraph::Resource> inputs,
                                                          std::string_view                       pass)
        {
            if (!graph.ownsResource(resource))
            {
                return std::unexpected(std::string(pass) + "." + port.name + ": resource belongs to another graph");
            }
            const auto info  = graph.resourceInfo(resource);
            const auto label = std::string(pass) + "." + port.name;
            if (info.isTexture != (port.kind == PassResourceKind::eTexture))
            {
                return std::unexpected(label + ": resource type mismatch");
            }
            if (info.isTexture && port.format != VriFormat_Unknown && info.textureDesc.format != port.format)
            {
                return std::unexpected(label + ": texture format mismatch");
            }
            if (port.sameExtentAsInput)
            {
                const auto input = inputs[*port.sameExtentAsInput];
                if (!graph.ownsResource(input))
                {
                    return std::unexpected(label + ": extent reference belongs to another graph");
                }
                const auto reference = graph.resourceInfo(input);
                if (!reference.isTexture || info.textureDesc.width != reference.textureDesc.width ||
                    info.textureDesc.height != reference.textureDesc.height)
                {
                    return std::unexpected(label + ": texture extent mismatch");
                }
            }
            return {};
        }

        std::expected<void, std::string> validateDefinition(const PassDefinition& definition)
        {
            if (definition.type.empty() || !definition.createExpected)
            {
                return std::unexpected("Pass definition needs a type name and an Expected factory");
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
                    return std::unexpected("Invalid pass parameter: " + parameter.name);
                }
                if (parameter.control < PassControl::eSlider || parameter.control > PassControl::eReadOnly ||
                    (parameter.control == PassControl::eChoice && parameter.choices.empty()) ||
                    (parameter.control == PassControl::eCheckbox &&
                     (parameter.minimum != 0 || parameter.maximum != 1 ||
                      (parameter.defaultValue != 0 && parameter.defaultValue != 1))))
                {
                    return std::unexpected("Invalid pass parameter control: " + parameter.name);
                }
                std::set<double> choiceValues;
                for (const auto& choice : parameter.choices)
                {
                    if (choice.label.empty() || !std::isfinite(choice.value) || choice.value < parameter.minimum ||
                        choice.value > parameter.maximum || !choiceValues.insert(choice.value).second)
                    {
                        return std::unexpected("Invalid pass parameter choice: " + parameter.name);
                    }
                }
            }
            if (auto values = definition.parameterValuesExpected(definition.type); !values)
            {
                return std::unexpected(values.error());
            }
            return {};
        }
    } // namespace

    std::expected<std::vector<double>, std::string>
    PassDefinition::parameterValuesExpected(std::string_view instance, const PassParameters& supplied) const
    {
        for (const auto& [key, value] : supplied)
        {
            if (!std::ranges::any_of(parameters,
                                     [&](const auto& parameter)
                                     {
                                         return parameter.name == key;
                                     }))
            {
                return std::unexpected(std::string(instance) + ": unknown parameter: " + key);
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
                return std::unexpected(std::string(instance) + ": parameter out of range: " + parameter.name);
            }
            if ((parameter.control == PassControl::eCheckbox && value != 0 && value != 1) ||
                (!parameter.choices.empty() && !std::ranges::any_of(parameter.choices,
                                                                    [&](const auto& choice)
                                                                    {
                                                                        return choice.value == value;
                                                                    })))
            {
                return std::unexpected(std::string(instance) + ": invalid discrete parameter: " + parameter.name);
            }
            values.push_back(value);
        }
        return values;
    }

    PassCatalog::PassCatalog(Device& device) :
        m_Device(device)
    {
    }

    void PassCatalog::add(PassDefinition definition)
    {
        if (auto added = addExpected(std::move(definition)); !added)
        {
            throw std::invalid_argument(added.error());
        }
    }

    std::expected<void, std::string> PassCatalog::addExpected(PassDefinition definition)
    {
        std::vector<PassDefinition> definitions;
        definitions.push_back(std::move(definition));
        return addDefinitionsExpected(std::move(definitions));
    }

    std::expected<void, std::string> PassCatalog::addDefinitionsExpected(std::vector<PassDefinition> definitions)
    {
        std::set<std::string_view> names;
        for (const auto& definition : m_Definitions)
        {
            names.insert(definition.type);
        }
        for (const auto& definition : definitions)
        {
            if (auto valid = validateDefinition(definition); !valid)
            {
                return valid;
            }
            if (!names.insert(definition.type).second)
            {
                return std::unexpected("Duplicate pass type: " + definition.type);
            }
        }
        if (!definitions.empty())
        {
            m_Definitions.reserve(m_Definitions.size() + definitions.size());
            for (auto& definition : definitions)
            {
                m_Definitions.push_back(std::move(definition));
            }
        }
        return {};
    }

    std::expected<void, std::string> PassCatalog::bindFactoryExpected(
        std::string_view                                                               type,
        std::function<std::expected<std::unique_ptr<GraphPass>, std::string>(Device&)> create)
    {
        if (!create)
        {
            return std::unexpected("Pass binding requires a factory");
        }
        const auto found = std::ranges::find(m_Definitions, type, &PassDefinition::type);
        if (found == m_Definitions.end())
        {
            return std::unexpected("Unknown bound pass type: " + std::string(type));
        }
        found->createExpected = std::move(create);
        return {};
    }

    const PassDefinition& PassCatalog::definition(std::string_view type) const
    {
        auto found = definitionExpected(type);
        if (!found)
        {
            throw std::invalid_argument(found.error());
        }
        return **found;
    }

    std::expected<const PassDefinition*, std::string> PassCatalog::definitionExpected(std::string_view type) const
    {
        const auto found = std::ranges::find(m_Definitions, type, &PassDefinition::type);
        if (found == m_Definitions.end())
        {
            return std::unexpected("Unknown pass type: " + std::string(type));
        }
        return &*found;
    }

    BuiltPass PassCatalog::build(RenderGraph&                           graph,
                                 std::string_view                       type,
                                 std::string_view                       name,
                                 std::span<const RenderGraph::Resource> inputs,
                                 const PassParameters&                  parameters) const
    {
        auto built = buildExpected(graph, type, name, inputs, parameters);
        if (!built)
        {
            throw std::invalid_argument(built.error());
        }
        return std::move(*built);
    }

    std::expected<BuiltPass, std::string> PassCatalog::buildExpected(RenderGraph&                           graph,
                                                                     std::string_view                       type,
                                                                     std::string_view                       name,
                                                                     std::span<const RenderGraph::Resource> inputs,
                                                                     const PassParameters& parameters) const
    {
        if (&graph.device() != &m_Device)
        {
            return std::unexpected("Pass catalog and graph belong to different devices");
        }
        if (auto editable = graph.checkGraphEditExpected(); !editable)
        {
            return std::unexpected(editable.error());
        }
        auto definition = definitionExpected(type);
        if (!definition)
        {
            return std::unexpected(definition.error());
        }
        const auto& desc = **definition;
        if (name.empty() || inputs.size() != desc.inputs.size())
        {
            return std::unexpected(std::string(name) + ": pass input count mismatch or empty instance name");
        }
        if ((m_Device.core.GetDeviceDesc(m_Device.handle)->enabledFeatures & desc.requiredFeatures) !=
            desc.requiredFeatures)
        {
            return std::unexpected(std::string(name) + ": required VRI features were not enabled");
        }
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (auto valid = validateResource(graph, desc.inputs[i], inputs[i], inputs, name); !valid)
            {
                return std::unexpected(valid.error());
            }
        }
        auto values = desc.parameterValuesExpected(name, parameters);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        BuiltPass result;
        result.graph           = &graph;
        result.name            = name;
        result.type            = type;
        result.parameterValues = std::move(*values);
        // Declared after result so rollback removes callbacks before result.instance is destroyed.
        RenderGraph::AppendTransaction transaction(graph);
        const auto                     firstPass = graph.m_Passes.size();
        auto                           instance  = desc.createExpected(m_Device);
        if (!instance)
        {
            return std::unexpected(std::string(name) + ": " + instance.error());
        }
        result.instance = std::move(*instance);
        if (!result.instance)
        {
            return std::unexpected(std::string(name) + ": pass factory returned no instance");
        }
        auto outputs = result.instance->addPassesExpected(graph, name, inputs, result.parameterValues);
        if (!outputs)
        {
            return std::unexpected(std::string(name) + ": " + outputs.error());
        }
        result.outputs = std::move(*outputs);
        for (size_t i = firstPass; i < graph.m_Passes.size(); ++i)
        {
            if (!graph.m_Passes[i].executeExpected)
            {
                return std::unexpected(std::string(name) + ": pass construction requires Expected command callbacks");
            }
            if (!desc.sideEffect && graph.m_Passes[i].sideEffect)
            {
                return std::unexpected(std::string(name) + ": side effects must be declared in PassDefinition");
            }
            graph.m_Passes[i].sideEffect = desc.sideEffect;
            result.commandPasses.push_back(graph.m_Passes[i].name);
        }
        if (result.outputs.size() != desc.outputs.size())
        {
            return std::unexpected(std::string(name) + ": pass output count mismatch");
        }
        for (size_t i = 0; i < result.outputs.size(); ++i)
        {
            if (auto valid = validateResource(graph, desc.outputs[i], result.outputs[i], inputs, name); !valid)
            {
                return std::unexpected(valid.error());
            }
        }
        transaction.commit();
        return result;
    }

    std::expected<void, std::string> PassCatalog::setParametersExpected(BuiltPass&            pass,
                                                                        const PassParameters& parameters) const
    {
        if (!pass.graph || &pass.graph->device() != &m_Device)
        {
            return std::unexpected(pass.name + ": pass and catalog require a graph on the same device");
        }
        const auto definition = std::ranges::find(m_Definitions, pass.type, &PassDefinition::type);
        if (definition == m_Definitions.end())
        {
            return std::unexpected(pass.name + ": unknown pass type: " + pass.type);
        }
        auto values = definition->parameterValuesExpected(pass.name, parameters);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        if (values->size() != pass.parameterValues.size())
        {
            return std::unexpected(pass.name + ": compiled parameter layout changed");
        }
        if (*values == pass.parameterValues)
        {
            return {};
        }
        std::vector<std::string_view> changed;
        changed.reserve(pass.commandPasses.size());
        for (const auto& name : pass.commandPasses)
        {
            changed.push_back(name);
        }
        if (auto reset = pass.graph->resetDependentHistoryExpected(changed); !reset)
        {
            return reset;
        }
        std::ranges::copy(*values, pass.parameterValues.begin());
        return {};
    }
} // namespace vultra
