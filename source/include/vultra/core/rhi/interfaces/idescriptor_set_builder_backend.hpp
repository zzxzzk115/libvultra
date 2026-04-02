#pragma once

#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/resource_binding.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/structs/handles.hpp"

namespace vultra
{
    namespace rhi
    {
        class IDescriptorSetBuilderBackend
        {
        public:
            virtual ~IDescriptorSetBuilderBackend() = default;

            virtual void bind(BindingIndex, const ResourceBinding&) = 0;
            virtual void bind(BindingIndex, const bindings::SeparateSampler&) = 0;
            virtual void bind(BindingIndex, const bindings::CombinedImageSampler&) = 0;
            virtual void bind(BindingIndex, const bindings::CombinedImageSamplerArray&) = 0;
            virtual void bind(BindingIndex, const bindings::SampledImage&) = 0;
            virtual void bind(BindingIndex, const bindings::StorageImage&) = 0;
            virtual void bind(BindingIndex, const bindings::UniformBuffer&) = 0;
            virtual void bind(BindingIndex, const bindings::StorageBuffer&) = 0;
            virtual void bind(BindingIndex, const bindings::AccelerationStructureKHR&) = 0;

            [[nodiscard]] virtual DescriptorSetHandle build(DescriptorSetLayoutKey) = 0;
        };
    } // namespace rhi
} // namespace vultra
