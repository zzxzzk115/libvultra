#pragma once

#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        constexpr auto kMinNumDescriptorSets = 4;

        struct DescriptorSetLayoutBindingEx
        {
            BindingIndex    binding {0};
            DescriptorType  type {DescriptorType::eSampler};
            uint32_t        count {1};
            ShaderStages    stageFlags {ShaderStages::eNone};
            uint32_t        flags {0};
        };

        struct PushConstantRange
        {
            uint32_t     offset {0};
            uint32_t     size {0};
            ShaderStages stageFlags {ShaderStages::eNone};
        };

        struct PipelineLayoutInfo
        {
            using DescriptorSetBindings = std::vector<DescriptorSetLayoutBindingEx>;
            std::array<DescriptorSetBindings, kMinNumDescriptorSets> descriptorSets;
            std::vector<PushConstantRange>                           pushConstantRanges;
        };
    } // namespace rhi
} // namespace vultra

