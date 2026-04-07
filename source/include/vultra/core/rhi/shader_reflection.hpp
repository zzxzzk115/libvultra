#pragma once

#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <vshadersystem/types.hpp>

#include <glm/ext/vector_uint3.hpp>
#include <array>
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
                explicit Descriptor(DescriptorType type) : type {type} {}

                DescriptorType                type {DescriptorType::eSampler};
                vshadersystem::ResourceAccess access {vshadersystem::ResourceAccess::eUnknown};
                uint32_t                      count {1};
                ShaderStages                  stageFlags {ShaderStages::eNone};
                uint32_t                      flags {0};
            };
            // Key = binding
            // layout(binding = index)
            using DescriptorSet = std::unordered_map<BindingIndex, Descriptor>;
            std::array<DescriptorSet, 4> descriptorSets;
            struct PushConstantRange
            {
                uint32_t     offset {0};
                uint32_t     size {0};
                ShaderStages stageFlags {ShaderStages::eNone};
            };
            std::vector<PushConstantRange> pushConstantRanges;
        };
    } // namespace rhi
} // namespace vultra
