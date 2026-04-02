#pragma once

#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/native_handles.hpp"
#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"
#include "vultra/core/rhi/sampler.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

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
        using DescriptorSetCache = std::unordered_map<std::size_t, DescriptorSetHandle>;

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
                uint64_t                      offset {0};
                std::optional<uint64_t>       range;
            };
            struct StorageBuffer
            {
                const Buffer*                 buffer {nullptr};
                uint64_t                      offset {0};
                std::optional<uint64_t>       range;
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

            [[nodiscard]] DescriptorSetHandle build(DescriptorSetLayoutKey);

        private:
            void clear();

            void addImage(std::uintptr_t imageView, ImageLayout imageLayout);
            void addSampler(const Sampler);
            void addCombinedImageSampler(std::uintptr_t imageView, ImageLayout imageLayout, Sampler);
            void addAccelerationStructure(std::uintptr_t);

            DescriptorSetBuilder& bindBuffer(BindingIndex, DescriptorType type, std::uintptr_t bufferHandle, uint64_t offset, uint64_t range);

        private:
            std::uintptr_t          m_Device {0};
            const RenderDevice*     m_RenderDevice {nullptr};
            DescriptorSetAllocator& m_DescriptorSetAllocator;
            DescriptorSetCache&     m_DescriptorSetCache;

            struct BindingInfo
            {
                DescriptorType type;
                uint32_t           count {0};
                int32_t            descriptorId {-1}; // Index to m_Descriptors
            };

            // layout(binding = index)
            std::unordered_map<BindingIndex, BindingInfo> m_Bindings;

            struct ImageInfo
            {
                std::uintptr_t imageView {0};
                std::uintptr_t sampler {0};
                ImageLayout    imageLayout {ImageLayout::eUndefined};
            };
            struct BufferInfo
            {
                std::uintptr_t buffer {0};
                uint64_t       offset {0};
                uint64_t       range {0};
            };
            std::vector<ImageInfo>         m_ImageInfos;
            std::vector<BufferInfo>        m_BufferInfos;
            std::vector<std::uintptr_t>    m_AccelerationStructures;
        };

        [[nodiscard]] std::string_view toString(const ResourceBinding&);
    } // namespace rhi

    using ResourceBindings = std::unordered_map<rhi::BindingIndex, rhi::ResourceBinding>;
    using ResourceSet      = std::unordered_map<rhi::DescriptorSetIndex, ResourceBindings>;
    using Samplers         = std::unordered_map<std::string, rhi::Sampler>;
} // namespace vultra
