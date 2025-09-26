#pragma once

#include "vultra/core/rhi/image_aspect.hpp"
#include "vultra/core/rhi/resource_indices.hpp"

#include <vulkan/vulkan.hpp>

#include <unordered_map>
#include <variant>

namespace vultra
{
    namespace rhi
    {
        class DescriptorSetAllocator;
        class Buffer;
        class Texture;
        class AccelerationStructure;

        // Key = Hash.
        using DescriptorSetCache = std::unordered_map<std::size_t, vk::DescriptorSet>;

        namespace bindings
        {
            struct SeparateSampler
            {
                vk::Sampler handle {nullptr};
            };
            struct CombinedImageSampler
            {
                const Texture*             texture {nullptr};
                ImageAspect                imageAspect {ImageAspect::eNone};
                std::optional<vk::Sampler> sampler;
            };
            struct SampledImage
            {
                const Texture* texture {nullptr};
                ImageAspect    imageAspect {ImageAspect::eNone};
            };
            struct StorageImage
            {
                const Texture*          texture {nullptr};
                ImageAspect             imageAspect {ImageAspect::eNone};
                std::optional<uint32_t> mipLevel;
            };

            struct UniformBuffer
            {
                const Buffer*                 buffer {nullptr};
                vk::DeviceSize                offset {0};
                std::optional<vk::DeviceSize> range;
            };
            struct StorageBuffer
            {
                const Buffer*                 buffer {nullptr};
                vk::DeviceSize                offset {0};
                std::optional<vk::DeviceSize> range;
            };

            struct AccelerationStructureKHR
            {
                const AccelerationStructure* as {nullptr};
            };
        } // namespace bindings

        using ResourceBinding = std::variant<bindings::SeparateSampler,
                                             bindings::CombinedImageSampler,
                                             bindings::SampledImage,
                                             bindings::StorageImage,
                                             bindings::UniformBuffer,
                                             bindings::StorageBuffer,
                                             bindings::AccelerationStructureKHR>;

        class DescriptorSetBuilder final
        {
        public:
            DescriptorSetBuilder() = delete;
            DescriptorSetBuilder(const vk::Device, DescriptorSetAllocator&, DescriptorSetCache&);
            DescriptorSetBuilder(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder(DescriptorSetBuilder&&) noexcept = delete;

            DescriptorSetBuilder& operator=(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder& operator=(DescriptorSetBuilder&&) noexcept = delete;

            DescriptorSetBuilder& bind(const BindingIndex, const ResourceBinding&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SeparateSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::CombinedImageSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SampledImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::UniformBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::AccelerationStructureKHR&);

            [[nodiscard]] vk::DescriptorSet build(const vk::DescriptorSetLayout);

        private:
            void clear();

            void addImage(const vk::ImageView, const vk::ImageLayout);
            void addSampler(const vk::Sampler);
            void addCombinedImageSampler(const vk::ImageView, const vk::ImageLayout, const vk::Sampler);
            void addAccelerationStructure(const vk::AccelerationStructureKHR&);

            DescriptorSetBuilder& bindBuffer(const BindingIndex, const vk::DescriptorType, vk::DescriptorBufferInfo&&);

        private:
            vk::Device              m_Device {nullptr};
            DescriptorSetAllocator& m_DescriptorSetAllocator;
            DescriptorSetCache&     m_DescriptorSetCache;

            struct BindingInfo
            {
                vk::DescriptorType type;
                uint32_t           count {0};
                int32_t            descriptorId {-1}; // Index to m_descriptors.
            };
            // layout(binding = index)
            std::unordered_map<BindingIndex, BindingInfo> m_Bindings;
            union DescriptorVariant
            {
                vk::DescriptorImageInfo                        imageInfo;
                vk::DescriptorBufferInfo                       bufferInfo;
                vk::WriteDescriptorSetAccelerationStructureKHR asInfo;
            };
            std::vector<DescriptorVariant>            m_Descriptors;
            std::vector<vk::AccelerationStructureKHR> m_AccelerationStructures; // Keep alive
        };

        [[nodiscard]] std::string_view toString(const ResourceBinding&);
    } // namespace rhi
} // namespace vultra