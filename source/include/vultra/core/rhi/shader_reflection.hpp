#pragma once

#include "vultra/core/rhi/resource_indices.hpp"

#include <vshadersystem/types.hpp>

#include <glm/ext/vector_uint3.hpp>
#include <vulkan/vulkan.hpp>

#include <optional>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        struct ShaderReflection
        {
            // Accumulate reflection information computed by vshadersystem.
            void accumulate(const vshadersystem::ShaderReflection&);

            std::optional<glm::uvec3> localSize; // ComputeShader only.

            struct Descriptor
            {
                explicit Descriptor(vk::DescriptorType type) : type {type} {}

                vk::DescriptorType       type {vk::DescriptorType::eSampler};
                uint32_t                 count {1};
                vk::ShaderStageFlags     stageFlags {0};
                VkDescriptorBindingFlags flags {0};
            };
            // Key = binding
            // layout(binding = index)
            using DescriptorSet = std::unordered_map<BindingIndex, Descriptor>;
            std::array<DescriptorSet, 4>       descriptorSets;
            std::vector<vk::PushConstantRange> pushConstantRanges;
        };
    } // namespace rhi
} // namespace vultra