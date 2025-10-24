#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/descriptorset_allocator.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace std
{
    template<>
    struct hash<vk::ImageView>
    {
        size_t operator()(const vk::ImageView& imageView) const noexcept { return hash<VkImageView>()(imageView); }
    };

    template<>
    struct hash<vk::Buffer>
    {
        size_t operator()(const vk::Buffer& buffer) const noexcept { return hash<VkBuffer>()(buffer); }
    };
} // namespace std

namespace vultra
{
    namespace rhi
    {

        // Defined in command_buffer.cpp
        [[nodiscard]] vk::ImageAspectFlags toVk(ImageAspect imageAspect);

#if _DEBUG
        namespace
        {
            [[nodiscard]] auto
            validRange(const Buffer& buffer, const vk::DeviceSize offset, const std::optional<vk::DeviceSize> range)
            {
                const auto size = buffer.getSize();
                if (offset > size)
                    return false;

                return !range || (offset + *range < size);
            }

        } // namespace
#endif
        DescriptorSetBuilder::DescriptorSetBuilder(const vk::Device        device,
                                                   DescriptorSetAllocator& allocator,
                                                   DescriptorSetCache&     cache) :
            m_Device(device), m_DescriptorSetAllocator(allocator), m_DescriptorSetCache(cache)
        {
            m_Bindings.reserve(10);
            m_ImageInfos.reserve(10);
            m_BufferInfos.reserve(10);
            m_ASInfos.reserve(2);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const ResourceBinding& r)
        {
            return std::visit([this, index](auto& info) -> decltype(auto) { return bind(index, info); }, r);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex               index,
                                                         const bindings::SeparateSampler& info)
        {
            m_Bindings[index] = {vk::DescriptorType::eSampler, 1, static_cast<int32_t>(m_ImageInfos.size())};
            addSampler(info.handle);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                    index,
                                                         const bindings::CombinedImageSampler& info)
        {
            m_Bindings[index] = {
                vk::DescriptorType::eCombinedImageSampler, 1, static_cast<int32_t>(m_ImageInfos.size())};
            const auto sampler = info.sampler.value_or(info.texture->getSampler());
            assert(sampler != VK_NULL_HANDLE);
            const auto imageLayout = info.texture->getImageLayout();
            assert(imageLayout != ImageLayout::eUndefined);

            addCombinedImageSampler(
                info.texture->getImageView(toVk(info.imageAspect)), static_cast<vk::ImageLayout>(imageLayout), sampler);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                         index,
                                                         const bindings::CombinedImageSamplerArray& info)
        {
            const auto numImages = static_cast<uint32_t>(info.textures.size());
            assert(numImages > 0);
            m_Bindings[index] = {
                vk::DescriptorType::eCombinedImageSampler, numImages, static_cast<int32_t>(m_ImageInfos.size())};
            for (const auto* texture : info.textures)
            {
                const auto imageLayout = texture->getImageLayout();
                assert(imageLayout != ImageLayout::eUndefined);
                const auto sampler = info.sampler.value_or(texture->getSampler());
                assert(sampler != VK_NULL_HANDLE);
                addCombinedImageSampler(
                    texture->getImageView(toVk(info.imageAspect)), static_cast<vk::ImageLayout>(imageLayout), sampler);
            }
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::SampledImage& info)
        {
            m_Bindings[index] = {vk::DescriptorType::eSampledImage, 1, static_cast<int32_t>(m_ImageInfos.size())};
            addImage(info.texture->getImageView(toVk(info.imageAspect)),
                     static_cast<vk::ImageLayout>(info.texture->getImageLayout()));
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageImage& info)
        {
            const auto numImages = info.mipLevel ? 1 : info.texture->getNumMipLevels();
            m_Bindings[index]    = {
                vk::DescriptorType::eStorageImage, numImages, static_cast<int32_t>(m_ImageInfos.size())};
            for (uint32_t i = 0; i < numImages; ++i)
                addImage(info.texture->getMipLevel(i, toVk(info.imageAspect)),
                         static_cast<vk::ImageLayout>(info.texture->getImageLayout()));
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::UniformBuffer& info)
        {
            return bindBuffer(index,
                              vk::DescriptorType::eUniformBuffer,
                              {info.buffer->getHandle(), info.offset, info.range.value_or(vk::WholeSize)});
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageBuffer& info)
        {
            return bindBuffer(index,
                              vk::DescriptorType::eStorageBuffer,
                              {info.buffer->getHandle(), info.offset, info.range.value_or(vk::WholeSize)});
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                        index,
                                                         const bindings::AccelerationStructureKHR& info)
        {
            m_Bindings[index] = {
                vk::DescriptorType::eAccelerationStructureKHR, 1, static_cast<int32_t>(m_ASInfos.size())};
            addAccelerationStructure(info.as->getHandle());
            return *this;
        }

        vk::DescriptorSet DescriptorSetBuilder::build(const vk::DescriptorSetLayout layout)
        {
            auto                                hash = std::bit_cast<std::size_t>(layout);
            std::vector<vk::WriteDescriptorSet> writes;
            writes.reserve(m_Bindings.size());

            for (auto& [idx, binding] : m_Bindings)
            {
                vk::WriteDescriptorSet record {};
                record.dstBinding      = idx;
                record.descriptorCount = binding.count;
                record.descriptorType  = binding.type;

                switch (binding.type)
                {
                    case vk::DescriptorType::eSampler:
                    case vk::DescriptorType::eCombinedImageSampler:
                    case vk::DescriptorType::eSampledImage:
                    case vk::DescriptorType::eStorageImage:
                        record.pImageInfo = &m_ImageInfos[binding.descriptorId];
                        hashCombine(hash, record.pImageInfo->imageView);
                        break;

                    case vk::DescriptorType::eUniformBuffer:
                    case vk::DescriptorType::eStorageBuffer:
                        record.pBufferInfo = &m_BufferInfos[binding.descriptorId];
                        hashCombine(
                            hash, record.pBufferInfo->offset, record.pBufferInfo->range, record.pBufferInfo->buffer);
                        break;

                    case vk::DescriptorType::eAccelerationStructureKHR:
                        record.pNext = &m_ASInfos[binding.descriptorId];
                        hashCombine(
                            hash,
                            reinterpret_cast<const vk::WriteDescriptorSetAccelerationStructureKHR*>(record.pNext)
                                ->pAccelerationStructures);
                        break;

                    default:
                        assert(false);
                }
                writes.emplace_back(record);
            }

            vk::DescriptorSet set;
            if (auto it = m_DescriptorSetCache.find(hash); it != m_DescriptorSetCache.end())
            {
                set = it->second;
            }
            else
            {
                uint32_t variableDescriptorCount = 0;
                for (const auto& [_, binding] : m_Bindings)
                {
                    if (binding.type == vk::DescriptorType::eCombinedImageSampler && binding.count > 1)
                    {
                        variableDescriptorCount = binding.count;
                        break;
                    }
                }
                set = m_DescriptorSetAllocator.allocate(layout, variableDescriptorCount);
                for (auto& r : writes)
                    r.dstSet = set;
                m_Device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                m_DescriptorSetCache.emplace(hash, set);
            }

            clear();
            return set;
        }

        void DescriptorSetBuilder::clear()
        {
            m_Bindings.clear();
            m_ImageInfos.clear();
            m_BufferInfos.clear();
            m_ASInfos.clear();
            m_AccelerationStructures.clear();
        }

        void DescriptorSetBuilder::addImage(const vk::ImageView view, const vk::ImageLayout layout)
        {
            m_ImageInfos.emplace_back(vk::DescriptorImageInfo {nullptr, view, layout});
        }

        void DescriptorSetBuilder::addSampler(const vk::Sampler sampler)
        {
            m_ImageInfos.emplace_back(vk::DescriptorImageInfo {sampler, nullptr, vk::ImageLayout::eUndefined});
        }

        void DescriptorSetBuilder::addCombinedImageSampler(const vk::ImageView   view,
                                                           const vk::ImageLayout layout,
                                                           const vk::Sampler     sampler)
        {
            m_ImageInfos.emplace_back(vk::DescriptorImageInfo {sampler, view, layout});
        }

        void DescriptorSetBuilder::addAccelerationStructure(const vk::AccelerationStructureKHR& as)
        {
            m_AccelerationStructures.push_back(as);
            vk::WriteDescriptorSetAccelerationStructureKHR info {};
            info.accelerationStructureCount = 1;
            info.pAccelerationStructures    = &m_AccelerationStructures.back();
            m_ASInfos.emplace_back(info);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bindBuffer(const BindingIndex         index,
                                                               const vk::DescriptorType   type,
                                                               vk::DescriptorBufferInfo&& buf)
        {
            m_Bindings[index] = {type, 1, static_cast<int32_t>(m_BufferInfos.size())};
            m_BufferInfos.emplace_back(std::move(buf));
            return *this;
        }

        std::string_view toString(const ResourceBinding& rb)
        {
#define CASE(T) [](const bindings::T&) { return #T; }

            return std::visit(
                Overload {
                    CASE(SeparateSampler),
                    CASE(CombinedImageSampler),
                    CASE(CombinedImageSamplerArray),
                    CASE(SampledImage),
                    CASE(StorageImage),
                    CASE(UniformBuffer),
                    CASE(StorageBuffer),
                    CASE(AccelerationStructureKHR),
                },
                rb);

#undef CASE
        }
    } // namespace rhi
} // namespace vultra