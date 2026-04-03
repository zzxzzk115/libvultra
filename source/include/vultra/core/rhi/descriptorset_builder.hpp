#pragma once

#include "vultra/core/rhi/interfaces/idescriptor_set_builder.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/resource_binding.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        // Key = Hash.
        using DescriptorSetCache = std::unordered_map<std::size_t, DescriptorSetHandle>;

        class DescriptorSetBuilder final
        {
        public:
            DescriptorSetBuilder() = delete;
            explicit DescriptorSetBuilder(std::unique_ptr<IDescriptorSetBuilder> impl);
            DescriptorSetBuilder(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder(DescriptorSetBuilder&&) noexcept = default;
            ~DescriptorSetBuilder()                               = default;

            DescriptorSetBuilder& operator=(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder& operator=(DescriptorSetBuilder&&) noexcept = default;

            DescriptorSetBuilder& bind(const BindingIndex, const ResourceBinding&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SeparateSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::CombinedImageSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::CombinedImageSamplerArray&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SampledImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::UniformBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::AccelerationStructureKHR&);

            [[nodiscard]] DescriptorSetHandle build(DescriptorSetLayoutKey);

        private:
            std::unique_ptr<IDescriptorSetBuilder> m_Impl;
        };

        [[nodiscard]] std::string_view toString(const ResourceBinding&);
    } // namespace rhi

    using ResourceBindings = std::unordered_map<rhi::BindingIndex, rhi::ResourceBinding>;
    using ResourceSet      = std::unordered_map<rhi::DescriptorSetIndex, ResourceBindings>;
    using Samplers         = std::unordered_map<std::string, rhi::Sampler>;
} // namespace vultra
