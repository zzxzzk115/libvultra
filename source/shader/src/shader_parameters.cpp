#include <vultra/shader/shader_program.hpp>

#include <algorithm>

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

    namespace
    {
        const ShaderAttribute* findAttribute(const std::vector<ShaderAttribute>& attributes, std::string_view name)
        {
            const auto found = std::ranges::find(attributes, name, &ShaderAttribute::name);
            return found == attributes.end() ? nullptr : &*found;
        }
    } // namespace

    const ShaderAttribute* ShaderParameter::attribute(std::string_view requested) const
    {
        return findAttribute(attributes, requested);
    }

    const ShaderAttribute* CompiledShader::attribute(std::string_view requested) const
    {
        return findAttribute(attributes, requested);
    }

} // namespace vultra
