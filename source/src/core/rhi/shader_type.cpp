#include "vultra/core/rhi/shader_type.hpp"

#include <magic_enum/magic_enum.hpp>

#include <bitset>

namespace vultra
{
    namespace rhi
    {
        std::string_view toString(const ShaderType shaderType) { return magic_enum::enum_name(shaderType); }

        ShaderStages getStage(const ShaderType shaderType)
        {
#define CASE(Value) \
    case ShaderType::e##Value: \
        return ShaderStages::e##Value

            switch (shaderType)
            {
                CASE(Vertex);
                CASE(Geometry);
                CASE(Fragment);
                CASE(Compute);
            }
#undef CASE

            assert(false);
            return ShaderStages {0};
        }

        uint8_t countStages(const ShaderStages flags)
        {
            return static_cast<uint8_t>(std::bitset<sizeof(ShaderStages) * 8> {std::to_underlying(flags)}.count());
        }
    } // namespace rhi
} // namespace vultra