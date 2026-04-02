#pragma once

#include "vultra/core/rhi/descriptorset_allocator.hpp"
#include "vultra/core/rhi/interfaces/idescriptor_set_builder_backend.hpp"
#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"

#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class VulkanDescriptorSetBuilderBackend final : public IDescriptorSetBuilderBackend
        {
        public:
            using Cache = std::unordered_map<std::size_t, DescriptorSetHandle>;

            VulkanDescriptorSetBuilderBackend(const RenderDevice&, std::uintptr_t deviceHandle, DescriptorSetAllocator&, Cache&);

            void bind(BindingIndex, const ResourceBinding&) override;
            void bind(BindingIndex, const bindings::SeparateSampler&) override;
            void bind(BindingIndex, const bindings::CombinedImageSampler&) override;
            void bind(BindingIndex, const bindings::CombinedImageSamplerArray&) override;
            void bind(BindingIndex, const bindings::SampledImage&) override;
            void bind(BindingIndex, const bindings::StorageImage&) override;
            void bind(BindingIndex, const bindings::UniformBuffer&) override;
            void bind(BindingIndex, const bindings::StorageBuffer&) override;
            void bind(BindingIndex, const bindings::AccelerationStructureKHR&) override;

            [[nodiscard]] DescriptorSetHandle build(DescriptorSetLayoutKey) override;

        private:
            void clear();

            void addImage(std::uintptr_t imageView, ImageLayout imageLayout);
            void addSampler(Sampler);
            void addCombinedImageSampler(std::uintptr_t imageView, ImageLayout imageLayout, Sampler);
            void addAccelerationStructure(std::uintptr_t);

            void bindBuffer(BindingIndex, DescriptorType type, std::uintptr_t bufferHandle, uint64_t offset, uint64_t range);

        private:
            std::uintptr_t          m_Device {0};
            const RenderDevice*     m_RenderDevice {nullptr};
            DescriptorSetAllocator* m_DescriptorSetAllocator {nullptr};
            Cache*                  m_DescriptorSetCache {nullptr};

            struct BindingInfo
            {
                DescriptorType type;
                uint32_t       count {0};
                int32_t        descriptorId {-1};
            };

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
            std::vector<ImageInfo>      m_ImageInfos;
            std::vector<BufferInfo>     m_BufferInfos;
            std::vector<std::uintptr_t> m_AccelerationStructures;
        };
    } // namespace rhi
} // namespace vultra
