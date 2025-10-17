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

            eRayGen,
            eMiss,
            eClosestHit,
            eAnyHit,
            eIntersect,

            eMesh,
            eTask,
        };

        [[nodiscard]] std::string_view toString(const ShaderType);

        enum class ShaderStages : VkShaderStageFlags
        {
            eVertex   = VK_SHADER_STAGE_VERTEX_BIT,
            eGeometry = VK_SHADER_STAGE_GEOMETRY_BIT,
            eFragment = VK_SHADER_STAGE_FRAGMENT_BIT,

            eCompute = VK_SHADER_STAGE_COMPUTE_BIT,

            eRayGen     = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            eMiss       = VK_SHADER_STAGE_MISS_BIT_KHR,
            eClosestHit = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            eAnyHit     = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            eIntersect  = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,

            eMesh = VK_SHADER_STAGE_MESH_BIT_EXT,
            eTask = VK_SHADER_STAGE_TASK_BIT_EXT,
        };

        [[nodiscard]] ShaderStages getStage(const ShaderType);
        [[nodiscard]] uint8_t      countStages(const ShaderStages);
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::ShaderStages> : std::true_type
{};