#pragma once

#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <vshadersystem/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        constexpr auto kMinNumDescriptorSets = 4;

        struct DescriptorSetLayoutKey
        {
            std::size_t value {0};

            constexpr DescriptorSetLayoutKey() = default;
            constexpr explicit DescriptorSetLayoutKey(std::size_t v) : value(v) {}
            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
        };

        struct DescriptorSetLayoutBindingEx
        {
            BindingIndex                binding {0};
            DescriptorType              type {DescriptorType::eSampler};
            vshadersystem::ResourceAccess access {vshadersystem::ResourceAccess::eUnknown};
            uint32_t                    count {1};
            ShaderStages                stageFlags {ShaderStages::eNone};
            uint32_t                    flags {0};
            // Texture view dimension for image bindings (WebGPU bind-group layout). eUnknown -> 2D.
            vshadersystem::TextureType  textureType {vshadersystem::TextureType::eUnknown};
            // This combined-image-sampler slot samples a depth texture. WebGPU forbids binding a depth view
            // to a filterable-float binding, so the layout must declare it unfilterable-float + a non-filtering
            // sampler. The cook can't tell a depth sampler2D from a colour one, so passes flag it explicitly.
            bool                        depthSampled {false};
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
