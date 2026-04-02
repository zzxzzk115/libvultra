#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/descriptorset_allocator.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/acceleration_structure.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"

namespace vultra
{
    namespace rhi
    {
#if _DEBUG
        namespace
        {
            [[nodiscard]] auto validRange(const Buffer& buffer, const uint64_t offset, const std::optional<uint64_t> range)
            {
                const auto size = buffer.getSize();
                if (offset > size)
                    return false;

                return !range || (offset + *range < size);
            }

        } // namespace
#endif
        DescriptorSetBuilder::DescriptorSetBuilder(const RenderDevice&    renderDevice,
                                                   const std::uintptr_t    deviceHandle,
                                                   DescriptorSetAllocator& allocator,
                                                   DescriptorSetCache&     cache) :
            m_Device(deviceHandle), m_RenderDevice(&renderDevice), m_DescriptorSetAllocator(allocator), m_DescriptorSetCache(cache)
        {
            m_Bindings.reserve(10);
            m_ImageInfos.reserve(10);
            m_BufferInfos.reserve(10);
            m_AccelerationStructures.reserve(2);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const ResourceBinding& r)
        {
            return std::visit([this, index](auto& info) -> decltype(auto) { return bind(index, info); }, r);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex               index,
                                                         const bindings::SeparateSampler& info)
        {
            m_Bindings[index] = {DescriptorType::eSampler, 1, static_cast<int32_t>(m_ImageInfos.size())};
            addSampler(info.handle);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                    index,
                                                         const bindings::CombinedImageSampler& info)
        {
            m_Bindings[index] = {
                DescriptorType::eCombinedImageSampler, 1, static_cast<int32_t>(m_ImageInfos.size())};
            const auto sampler = info.sampler.value_or(info.texture->getSampler());
            assert(sampler);
            const auto imageLayout = info.texture->getImageLayout();
            assert(imageLayout != ImageLayout::eUndefined);

            addCombinedImageSampler(info.texture->getImageView(toRhi(toVk(info.imageAspect))).getNativeHandle(), imageLayout, sampler);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                         index,
                                                         const bindings::CombinedImageSamplerArray& info)
        {
            const auto numImages = static_cast<uint32_t>(info.textures.size());
            assert(numImages > 0);
            m_Bindings[index] = {
                DescriptorType::eCombinedImageSampler, numImages, static_cast<int32_t>(m_ImageInfos.size())};
            for (const auto* texture : info.textures)
            {
                const auto imageLayout = texture->getImageLayout();
                assert(imageLayout != ImageLayout::eUndefined);
                const auto sampler = info.sampler.value_or(texture->getSampler());
                assert(sampler);
                addCombinedImageSampler(texture->getImageView(toRhi(toVk(info.imageAspect))).getNativeHandle(), imageLayout, sampler);
            }
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::SampledImage& info)
        {
            m_Bindings[index] = {DescriptorType::eSampledImage, 1, static_cast<int32_t>(m_ImageInfos.size())};
            addImage(info.texture->getImageView(toRhi(toVk(info.imageAspect))).getNativeHandle(), info.texture->getImageLayout());
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageImage& info)
        {
            const auto numImages = info.mipLevel ? 1 : info.texture->getNumMipLevels();
            m_Bindings[index]    = {
                DescriptorType::eStorageImage, numImages, static_cast<int32_t>(m_ImageInfos.size())};
            for (uint32_t i = 0; i < numImages; ++i)
                addImage(info.texture->getMipLevel(i, toRhi(toVk(info.imageAspect))).getNativeHandle(), info.texture->getImageLayout());
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::UniformBuffer& info)
        {
            return bindBuffer(index,
                              DescriptorType::eUniformBuffer,
                              info.buffer->getHandle(),
                              info.offset,
                              info.range.value_or(vk::WholeSize));
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageBuffer& info)
        {
            return bindBuffer(index,
                              DescriptorType::eStorageBuffer,
                              info.buffer->getHandle(),
                              info.offset,
                              info.range.value_or(vk::WholeSize));
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                        index,
                                                         const bindings::AccelerationStructureKHR& info)
        {
            m_Bindings[index] = {
                DescriptorType::eAccelerationStructure, 1, static_cast<int32_t>(m_AccelerationStructures.size())};
            addAccelerationStructure(info.as->getHandle());
            return *this;
        }

        DescriptorSetHandle DescriptorSetBuilder::build(const DescriptorSetLayoutKey layoutKey)
        {
            assert(m_RenderDevice);
            const auto layoutHandle = m_RenderDevice->getDescriptorSetLayoutNativeHandle(layoutKey);
            const auto layout = vk::DescriptorSetLayout {reinterpret_cast<VkDescriptorSetLayout>(layoutHandle)};
            auto hash         = std::hash<std::size_t>()(layoutKey.value);
            std::vector<vk::WriteDescriptorSet> writes;
            writes.reserve(m_Bindings.size());
            std::vector<vk::DescriptorImageInfo> vkImageInfos;
            vkImageInfos.resize(m_ImageInfos.size());
            std::vector<vk::DescriptorBufferInfo> vkBufferInfos;
            vkBufferInfos.resize(m_BufferInfos.size());
            std::vector<vk::AccelerationStructureKHR> vkAccelerationStructures;
            vkAccelerationStructures.resize(m_AccelerationStructures.size());
            std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> asInfos;
            asInfos.reserve(m_AccelerationStructures.size());

            for (auto& [idx, binding] : m_Bindings)
            {
                vk::WriteDescriptorSet record {};
                record.dstBinding      = idx;
                record.descriptorCount = binding.count;
                record.descriptorType  = toVk(binding.type);

                switch (binding.type)
                {
                    case DescriptorType::eSampler:
                    case DescriptorType::eCombinedImageSampler:
                    case DescriptorType::eSampledImage:
                    case DescriptorType::eStorageImage:
                    {
                        const auto begin = static_cast<size_t>(binding.descriptorId);
                        const auto end   = begin + static_cast<size_t>(binding.count);
                        assert(end <= m_ImageInfos.size());
                        assert(end <= vkImageInfos.size());

                        for (auto i = begin; i < end; ++i)
                        {
                            const auto& imageInfo = m_ImageInfos[i];
                            vkImageInfos[i]       = vk::DescriptorImageInfo {
                                vk::Sampler {reinterpret_cast<VkSampler>(imageInfo.sampler)},
                                vk::ImageView {reinterpret_cast<VkImageView>(imageInfo.imageView)},
                                toVk(imageInfo.imageLayout),
                            };
                            hashCombine(hash,
                                        imageInfo.imageView,
                                        imageInfo.sampler,
                                        static_cast<int>(imageInfo.imageLayout));
                        }
                        record.pImageInfo = &vkImageInfos[begin];
                        break;
                    }

                    case DescriptorType::eUniformBuffer:
                    case DescriptorType::eStorageBuffer:
                    {
                        const auto begin = static_cast<size_t>(binding.descriptorId);
                        const auto end   = begin + static_cast<size_t>(binding.count);
                        assert(end <= m_BufferInfos.size());
                        assert(end <= vkBufferInfos.size());

                        for (auto i = begin; i < end; ++i)
                        {
                            const auto& bufferInfo = m_BufferInfos[i];
                            vkBufferInfos[i]       = vk::DescriptorBufferInfo {
                                vk::Buffer {reinterpret_cast<VkBuffer>(bufferInfo.buffer)},
                                bufferInfo.offset,
                                bufferInfo.range,
                            };
                            hashCombine(hash, bufferInfo.offset, bufferInfo.range, bufferInfo.buffer);
                        }
                        record.pBufferInfo = &vkBufferInfos[begin];
                        break;
                    }

                    case DescriptorType::eAccelerationStructure:
                    {
                        const auto asHandle = m_AccelerationStructures[binding.descriptorId];
                        vkAccelerationStructures[static_cast<size_t>(binding.descriptorId)] =
                            vk::AccelerationStructureKHR {reinterpret_cast<VkAccelerationStructureKHR>(asHandle)};
                        asInfos.push_back(vk::WriteDescriptorSetAccelerationStructureKHR {}
                                              .setAccelerationStructures(vkAccelerationStructures[static_cast<size_t>(binding.descriptorId)]));
                        record.pNext = &asInfos.back();
                        hashCombine(hash, asHandle);
                        break;
                    }

                    default:
                        assert(false);
                }
                writes.emplace_back(record);
            }

            vk::DescriptorSet set {};
            if (auto it = m_DescriptorSetCache.find(hash); it != m_DescriptorSetCache.end())
            {
                set = vk::DescriptorSet {reinterpret_cast<VkDescriptorSet>(it->second.value)};
            }
            else
            {
                uint32_t variableDescriptorCount = 0;
                for (const auto& [_, binding] : m_Bindings)
                {
                    if (binding.type == DescriptorType::eCombinedImageSampler && binding.count > 1)
                    {
                        variableDescriptorCount = binding.count;
                        break;
                    }
                }
                const vk::Device device {reinterpret_cast<VkDevice>(m_Device)};
                const auto setHandle = m_DescriptorSetAllocator.allocate(layoutHandle, variableDescriptorCount);
                set = vk::DescriptorSet {reinterpret_cast<VkDescriptorSet>(setHandle.value)};
                for (auto& r : writes)
                    r.dstSet = set;
                device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                m_DescriptorSetCache.emplace(hash, setHandle);
            }

            clear();
            return DescriptorSetHandle {reinterpret_cast<std::uintptr_t>(static_cast<VkDescriptorSet>(set))};
        }

        void DescriptorSetBuilder::clear()
        {
            m_Bindings.clear();
            m_ImageInfos.clear();
            m_BufferInfos.clear();
            m_AccelerationStructures.clear();
        }

        void DescriptorSetBuilder::addImage(const std::uintptr_t imageView, const ImageLayout imageLayout)
        {
            m_ImageInfos.emplace_back(ImageInfo {
                .imageView = imageView,
                .sampler = 0,
                .imageLayout = imageLayout,
            });
        }

        void DescriptorSetBuilder::addSampler(const Sampler sampler)
        {
            assert(m_RenderDevice);
            m_ImageInfos.emplace_back(ImageInfo {
                .imageView = 0,
                .sampler = m_RenderDevice->getSamplerHandle(sampler).value,
                .imageLayout = ImageLayout::eUndefined,
            });
        }

        void DescriptorSetBuilder::addCombinedImageSampler(const std::uintptr_t imageView,
                                                           const ImageLayout    imageLayout,
                                                           const Sampler        sampler)
        {
            m_ImageInfos.emplace_back(ImageInfo {
                .imageView = imageView,
                .sampler = m_RenderDevice->getSamplerHandle(sampler).value,
                .imageLayout = imageLayout,
            });
        }

        void DescriptorSetBuilder::addAccelerationStructure(const std::uintptr_t as)
        {
            m_AccelerationStructures.push_back(as);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bindBuffer(const BindingIndex index,
                                                               const DescriptorType type,
                                                               const std::uintptr_t bufferHandle,
                                                               const uint64_t offset,
                                                               const uint64_t range)
        {
            m_Bindings[index] = {type, 1, static_cast<int32_t>(m_BufferInfos.size())};
            m_BufferInfos.emplace_back(BufferInfo {
                .buffer = bufferHandle,
                .offset = offset,
                .range = range,
            });
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
