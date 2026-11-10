#include <vultra/drivers/rhi/shader_program.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    uint64_t ShaderParameter::offset(ShaderOffsetKind kind) const
    {
        for (const auto& value : offsets)
        {
            if (value.kind == kind)
            {
                return value.offset;
            }
        }
        return 0;
    }

    uint64_t ShaderParameter::space(ShaderOffsetKind kind) const
    {
        for (const auto& value : offsets)
        {
            if (value.kind == kind)
            {
                return value.space;
            }
        }
        return 0;
    }

    const ShaderParameter* ShaderParameter::field(std::string_view requested) const
    {
        for (const auto& child : fields)
        {
            if (child.name == requested)
            {
                return &child;
            }
        }
        for (const auto& child : fields)
        {
            if (child.name == "$element" || child.name == "$container")
            {
                if (auto* found = child.field(requested))
                {
                    return found;
                }
            }
        }
        return nullptr;
    }

    std::expected<const ShaderParameter*, std::string> ShaderParameter::bindingExpected(std::string_view name) const
    {
        if (name.size() > 4096 || name.find(char(0)) != std::string_view::npos)
        {
            return std::unexpected("Invalid reflected binding path");
        }
        const auto             original = name;
        const ShaderParameter* node     = this;
        for (uint32_t depth = 0; node && depth <= 64; ++depth)
        {
            if (node->kind == ShaderParameterKind::eArray)
            {
                node = node->fields.size() == 1 ? &node->fields.front() : nullptr;
                continue;
            }
            if (name.empty())
            {
                return node;
            }
            if (node->kind == ShaderParameterKind::eConstantBuffer ||
                node->kind == ShaderParameterKind::eParameterBlock)
            {
                node = node->field("$element");
                continue;
            }
            const auto separator = name.find('.');
            node                 = node->field(name.substr(0, separator));
            name = separator == std::string_view::npos ? std::string_view {} : name.substr(separator + 1);
            if (separator != std::string_view::npos && name.empty())
            {
                break;
            }
        }
        return std::unexpected("Missing or invalid reflected binding parameter: " + std::string(original));
    }
} // namespace vultra
