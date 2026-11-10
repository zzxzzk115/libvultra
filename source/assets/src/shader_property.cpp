#include <vultra/assets/shader_property.hpp>

#include <cmath>
#include <stdexcept>

namespace vultra
{
    std::string ShaderSourceLocation::describe() const
    {
        return file.generic_string() + ":" + std::to_string(line) + ":" + std::to_string(column);
    }

    bool ShaderProperty::isTexture() const
    {
        return type >= ShaderPropertyType::eTexture2D && type <= ShaderPropertyType::eTextureCubeArray;
    }

    void ShaderProperty::validate(const ShaderPropertyValue& value) const
    {
        if (auto valid = validateExpected(value); !valid)
        {
            throw std::invalid_argument(valid.error());
        }
    }

    std::expected<void, std::string> ShaderProperty::validateExpected(const ShaderPropertyValue& value) const
    {
        auto fail = [&](std::string message)
        {
            return std::unexpected(location.describe() + ": property " + name + ": " + message);
        };
        if (type < ShaderPropertyType::eFloat || type > ShaderPropertyType::eTextureCubeArray)
        {
            return fail("Unknown declared property type");
        }
        size_t expected = 0;
        if (type == ShaderPropertyType::eInteger)
        {
            expected = 1;
        }
        else if (type == ShaderPropertyType::eBoolean)
        {
            expected = 2;
        }
        else if (type == ShaderPropertyType::eVector || type == ShaderPropertyType::eColor)
        {
            expected = 3;
        }
        else if (isTexture())
        {
            expected = 4;
        }
        if (value.index() != expected)
        {
            return fail("Value does not match the declared property type");
        }
        if (const auto* scalar = std::get_if<float>(&value); scalar && !std::isfinite(*scalar))
        {
            return fail("Value must be finite");
        }
        if (const auto* vector = std::get_if<std::array<float, 4>>(&value))
        {
            for (const auto component : *vector)
            {
                if (!std::isfinite(component))
                {
                    return fail("Components must be finite");
                }
            }
        }
        if (const auto* texture = std::get_if<ShaderTextureValue>(&value))
        {
            if ((!texture->builtin.empty() && texture->builtin != "white" && texture->builtin != "black" &&
                 texture->builtin != "normal") ||
                (texture->asset.value.valid() && !texture->builtin.empty()))
            {
                return fail("Texture must reference either one asset or a known builtin");
            }
            for (const auto component : texture->scaleOffset)
            {
                if (!std::isfinite(component))
                {
                    return fail("Texture scale/offset must be finite");
                }
            }
        }
        return {};
    }

} // namespace vultra
