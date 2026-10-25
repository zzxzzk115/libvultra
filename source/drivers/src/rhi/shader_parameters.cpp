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

} // namespace vultra
