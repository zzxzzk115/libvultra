#pragma once

#include "vultra/core/base/scoped_enum_flags.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        using SPIRV = std::vector<uint32_t>;

        enum class ShaderType
        {
            eVertex,
            eGeometry,
            eFragment,
            eCompute,
        };

        [[nodiscard]] std::string_view toString(const ShaderType);

        enum class ShaderStages : VkShaderStageFlags
        {
            eVertex   = VK_SHADER_STAGE_VERTEX_BIT,
            eGeometry = VK_SHADER_STAGE_GEOMETRY_BIT,
            eFragment = VK_SHADER_STAGE_FRAGMENT_BIT,
            eCompute  = VK_SHADER_STAGE_COMPUTE_BIT,
        };

        [[nodiscard]] ShaderStages getStage(const ShaderType);
        [[nodiscard]] uint8_t      countStages(const ShaderStages);
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::ShaderStages> : std::true_type
{};