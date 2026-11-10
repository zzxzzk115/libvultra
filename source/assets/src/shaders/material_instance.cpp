#include <vultra/assets/shader_asset.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

namespace vultra
{
    ShaderPropertyValue MaterialInstance::value(const ShaderAsset& asset, std::string_view name) const
    {
        auto value = valueExpected(asset, name);
        if (!value)
        {
            throw std::invalid_argument(value.error());
        }
        return std::move(*value);
    }

    std::expected<ShaderPropertyValue, std::string> MaterialInstance::valueExpected(const ShaderAsset& asset,
                                                                                    std::string_view   name) const
    {
        auto property = asset.propertyExpected(name);
        if (!property)
        {
            return std::unexpected(property.error());
        }
        const auto found = m_Overrides.find(name);
        return found == m_Overrides.end() ? (*property)->defaultValue : found->second;
    }

    void MaterialInstance::set(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value)
    {
        if (auto set = setExpected(asset, name, std::move(value)); !set)
        {
            throw std::invalid_argument(set.error());
        }
    }

    std::expected<void, std::string>
    MaterialInstance::setExpected(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value)
    {
        auto property = asset.propertyExpected(name);
        if (!property)
        {
            return std::unexpected(property.error());
        }
        if (auto valid = (*property)->validateExpected(value); !valid)
        {
            return valid;
        }
        const auto found = m_Overrides.find(name);
        if (found != m_Overrides.end() && found->second == value)
        {
            return {};
        }
        if (value == (*property)->defaultValue)
        {
            return resetExpected(name);
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material revision exhausted");
        }
        m_Overrides.insert_or_assign(std::string(name), std::move(value));
        ++m_Revision;
        return {};
    }

    void MaterialInstance::setVariant(const ShaderAsset& asset, std::string value)
    {
        if (auto selected = setVariantExpected(asset, std::move(value)); !selected)
        {
            throw std::invalid_argument(selected.error());
        }
    }

    std::expected<void, std::string> MaterialInstance::setVariantExpected(const ShaderAsset& asset, std::string value)
    {
        if (auto selected = asset.variantExpected(value); !selected)
        {
            return std::unexpected(selected.error());
        }
        if (variant == value)
        {
            return {};
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material revision exhausted");
        }
        variant = std::move(value);
        ++m_Revision;
        return {};
    }

    void MaterialInstance::validate(const ShaderAsset& asset) const
    {
        if (auto valid = validateExpected(asset); !valid)
        {
            throw std::invalid_argument(valid.error());
        }
    }

    std::expected<void, std::string> MaterialInstance::validateExpected(const ShaderAsset& asset) const
    {
        if (auto selected = asset.variantExpected(variant); !selected)
        {
            return std::unexpected(selected.error());
        }
        for (const auto& [name, value] : m_Overrides)
        {
            auto property = asset.propertyExpected(name);
            if (!property)
            {
                return std::unexpected(property.error());
            }
            if (auto valid = (*property)->validateExpected(value); !valid)
            {
                return valid;
            }
        }
        for (const auto& property : asset.properties)
        {
            const auto  found = m_Overrides.find(property.name);
            const auto& value = found == m_Overrides.end() ? property.defaultValue : found->second;
            if (auto valid = property.validateExpected(value); !valid)
            {
                return valid;
            }
            if (const auto* texture = std::get_if<ShaderTextureValue>(&value);
                texture && !texture->asset.value.valid() && texture->builtin.empty())
            {
                return std::unexpected(property.location.describe() + ": material requires texture: " + property.name);
            }
        }
        return {};
    }

    void
    MaterialInstance::reconcile(const ShaderAsset& previous, const ShaderAsset& candidate, std::string& diagnostics)
    {
        if (auto reconciled = reconcileExpected(previous, candidate, diagnostics); !reconciled)
        {
            throw std::invalid_argument(reconciled.error());
        }
    }

    std::expected<void, std::string> MaterialInstance::reconcileExpected(const ShaderAsset& previous,
                                                                         const ShaderAsset& candidate,
                                                                         std::string&       diagnostics)
    {
        if (auto valid = candidate.validateExpected(); !valid)
        {
            return valid;
        }
        MaterialInstance pending = *this;
        std::string      messages;
        for (const auto& property : previous.properties)
        {
            const auto found = std::ranges::find(candidate.properties, property.name, &ShaderProperty::name);
            if (found == candidate.properties.end())
            {
                messages += "Removed material property: " + property.name + "\n";
            }
            else if (found->type != property.type)
            {
                messages += "Reset type-changed material property: " + property.name + "\n";
            }
        }
        for (const auto& property : candidate.properties)
        {
            if (std::ranges::find(previous.properties, property.name, &ShaderProperty::name) ==
                previous.properties.end())
            {
                messages += "Added material property with shader default: " + property.name + "\n";
            }
        }
        std::erase_if(pending.m_Overrides,
                      [&](const auto& value)
                      {
                          const auto found =
                              std::ranges::find(candidate.properties, value.first, &ShaderProperty::name);
                          const auto old = std::ranges::find(previous.properties, value.first, &ShaderProperty::name);
                          return found == candidate.properties.end() || old == previous.properties.end() ||
                                 found->type != old->type;
                      });
        if (auto valid = pending.validateExpected(candidate); !valid)
        {
            return valid;
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material revision exhausted");
        }
        auto publishedDiagnostics = diagnostics + messages;
        m_Overrides.swap(pending.m_Overrides);
        diagnostics.swap(publishedDiagnostics);
        ++m_Revision;
        return {};
    }

    void MaterialInstance::assign(const ShaderAsset& asset, const MaterialInstance& source)
    {
        if (auto assigned = assignExpected(asset, source); !assigned)
        {
            throw std::invalid_argument(assigned.error());
        }
    }

    std::expected<void, std::string> MaterialInstance::assignExpected(const ShaderAsset&      asset,
                                                                      const MaterialInstance& source)
    {
        if (auto valid = source.validateExpected(asset); !valid)
        {
            return valid;
        }
        if (shader == source.shader && variant == source.variant && m_Overrides == source.m_Overrides)
        {
            return {};
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material revision exhausted");
        }
        auto overrides = source.m_Overrides;
        auto selected  = source.variant;
        m_Overrides.swap(overrides);
        variant.swap(selected);
        shader = source.shader;
        ++m_Revision;
        return {};
    }

    std::vector<std::byte> MaterialInstance::uniformData(const ShaderAsset& asset, const ShaderProgram& program) const
    {
        auto data = uniformDataExpected(asset, program);
        if (!data)
        {
            throw std::invalid_argument(data.error());
        }
        return std::move(*data);
    }

    std::expected<std::vector<std::byte>, std::string>
    MaterialInstance::uniformDataExpected(const ShaderAsset& asset, const ShaderProgram& program) const
    {
        auto uniforms = uniformBlockExpected(asset, program);
        if (!uniforms)
        {
            return std::unexpected(uniforms.error());
        }
        return std::vector<std::byte>(uniforms->data().begin(), uniforms->data().end());
    }

    std::expected<ShaderUniformBlock, std::string>
    MaterialInstance::uniformBlockExpected(const ShaderAsset& asset, const ShaderProgram& program) const
    {
        if (auto valid = validateExpected(asset); !valid)
        {
            return std::unexpected(valid.error());
        }
        const auto* block = program.parameters.field("material");
        if (!block || block->kind != ShaderParameterKind::eParameterBlock)
        {
            return std::unexpected("Program does not expose the generated material ParameterBlock");
        }
        auto uniforms = ShaderUniformBlock::create(*block);
        if (!uniforms)
        {
            return std::unexpected(uniforms.error());
        }
        const auto write = [&](std::string_view name, const auto values, ShaderParameterKind kind, uint32_t columns = 1)
            -> std::expected<void, std::string>
        {
            const std::array edits {ShaderUniformWrite {name, values, kind, 1, columns}};
            return uniforms->setExpected(edits).transform(
                [](bool)
                {
                });
        };
        for (const auto& property : asset.properties)
        {
            auto propertyValue = valueExpected(asset, property.name);
            if (!propertyValue)
            {
                return std::unexpected(propertyValue.error());
            }
            std::expected<void, std::string> result;
            if (const auto* scalar = std::get_if<float>(&*propertyValue))
            {
                result = write(property.name, std::span(scalar, 1), ShaderParameterKind::eScalar);
            }
            else if (const auto* integer = std::get_if<int32_t>(&*propertyValue))
            {
                result = write(property.name, std::span(integer, 1), ShaderParameterKind::eScalar);
            }
            else if (const auto* boolean = std::get_if<bool>(&*propertyValue))
            {
                result = write(property.name, std::span(boolean, 1), ShaderParameterKind::eScalar);
            }
            else if (const auto* vector = std::get_if<std::array<float, 4>>(&*propertyValue))
            {
                auto components = *vector;
                if (property.type == ShaderPropertyType::eColor && !property.hdr)
                {
                    for (size_t index = 0; index < 3; ++index)
                    {
                        const auto component = components[index];
                        components[index] =
                            component <= 0.04045f ? component / 12.92f : std::pow((component + 0.055f) / 1.055f, 2.4f);
                    }
                }
                result = write(property.name, std::span<const float>(components), ShaderParameterKind::eVector, 4);
            }
            else
            {
                const auto& texture = std::get<ShaderTextureValue>(*propertyValue);
                if (!property.noScaleOffset && (property.type == ShaderPropertyType::eTexture2D ||
                                                property.type == ShaderPropertyType::eTexture2DArray))
                {
                    result = write(property.name + "ScaleOffset",
                                   std::span(texture.scaleOffset),
                                   ShaderParameterKind::eVector,
                                   4);
                }
                if (result && property.normal)
                {
                    const uint32_t encoding = 0;
                    result = write(property.name + "Encoding", std::span(&encoding, 1), ShaderParameterKind::eScalar);
                }
            }
            if (!result)
            {
                return std::unexpected(result.error());
            }
        }
        return uniforms;
    }
} // namespace vultra
