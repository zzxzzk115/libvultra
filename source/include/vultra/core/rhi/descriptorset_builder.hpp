#pragma once

#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/sampler.hpp"

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <unordered_map>
#include <variant>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
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
                Sampler handle {};
            };
            struct CombinedImageSampler
            {
                const Texture*             texture {nullptr};
                ImageAspect                imageAspect {ImageAspect::eNone};
                std::optional<Sampler> sampler;
            };
            struct CombinedImageSamplerArray
            {
                std::vector<const Texture*> textures;
                ImageAspect                 imageAspect {ImageAspect::eNone};
                std::optional<Sampler>      sampler;
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
                                             bindings::CombinedImageSamplerArray,
                                             bindings::SampledImage,
                                             bindings::StorageImage,
                                             bindings::UniformBuffer,
                                             bindings::StorageBuffer,
                                             bindings::AccelerationStructureKHR>;

        class DescriptorSetBuilder final
        {
        public:
            DescriptorSetBuilder() = delete;
            DescriptorSetBuilder(const RenderDevice&, std::uintptr_t deviceHandle, DescriptorSetAllocator&, DescriptorSetCache&);
            DescriptorSetBuilder(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder(DescriptorSetBuilder&&) noexcept = delete;

            DescriptorSetBuilder& operator=(const DescriptorSetBuilder&)     = delete;
            DescriptorSetBuilder& operator=(DescriptorSetBuilder&&) noexcept = delete;

            DescriptorSetBuilder& bind(const BindingIndex, const ResourceBinding&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SeparateSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::CombinedImageSampler&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::CombinedImageSamplerArray&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::SampledImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageImage&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::UniformBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::StorageBuffer&);
            DescriptorSetBuilder& bind(const BindingIndex, const bindings::AccelerationStructureKHR&);

            [[nodiscard]] vk::DescriptorSet build(std::uintptr_t);

        private:
            void clear();

            void addImage(const vk::ImageView, const vk::ImageLayout);
            void addSampler(const Sampler);
            void addCombinedImageSampler(const vk::ImageView, const vk::ImageLayout, const Sampler);
            void addAccelerationStructure(const vk::AccelerationStructureKHR&);

            DescriptorSetBuilder& bindBuffer(const BindingIndex, const vk::DescriptorType, vk::DescriptorBufferInfo&&);

        private:
            std::uintptr_t          m_Device {0};
            const RenderDevice*     m_RenderDevice {nullptr};
            DescriptorSetAllocator& m_DescriptorSetAllocator;
            DescriptorSetCache&     m_DescriptorSetCache;

            struct BindingInfo
            {
                vk::DescriptorType type;
                uint32_t           count {0};
                int32_t            descriptorId {-1}; // Index to m_Descriptors
            };

            // layout(binding = index)
            std::unordered_map<BindingIndex, BindingInfo> m_Bindings;

            std::vector<vk::DescriptorImageInfo>                        m_ImageInfos;
            std::vector<vk::DescriptorBufferInfo>                       m_BufferInfos;
            std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> m_ASInfos;

            std::vector<vk::AccelerationStructureKHR> m_AccelerationStructures; // To keep the handles alive.
        };

        [[nodiscard]] std::string_view toString(const ResourceBinding&);
    } // namespace rhi

    using ResourceBindings = std::unordered_map<rhi::BindingIndex, rhi::ResourceBinding>;
    using ResourceSet      = std::unordered_map<rhi::DescriptorSetIndex, ResourceBindings>;
    using Samplers         = std::unordered_map<std::string, rhi::Sampler>;
} // namespace vultra

