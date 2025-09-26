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
                                                   DescriptorSetAllocator& descriptorSetAllocator,
                                                   DescriptorSetCache&     cache) :
            m_Device(device), m_DescriptorSetAllocator(descriptorSetAllocator), m_DescriptorSetCache(cache)
        {
            m_Bindings.reserve(10);
            m_Descriptors.reserve(10);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const ResourceBinding& r)
        {
            return std::visit([this, index](auto& info) -> decltype(auto) { return bind(index, info); }, r);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex               index,
                                                         const bindings::SeparateSampler& info)
        {
            assert(info.handle);

            m_Bindings[index] = BindingInfo {
                .type         = vk::DescriptorType::eSampler,
                .count        = 1,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };
            addSampler(info.handle);

            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                    index,
                                                         const bindings::CombinedImageSampler& info)
        {
            assert(info.texture && *info.texture);

            m_Bindings[index] = BindingInfo {
                .type         = vk::DescriptorType::eCombinedImageSampler,
                .count        = 1,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };

            const auto sampler = info.sampler.value_or(info.texture->getSampler());
            assert(sampler != VK_NULL_HANDLE);
            const auto imageLayout = info.texture->getImageLayout();
            assert(imageLayout != ImageLayout::eUndefined);

            m_Descriptors.emplace_back(DescriptorVariant {
                .imageInfo =
                    vk::DescriptorImageInfo {
                        sampler,
                        info.texture->getImageView(toVk(info.imageAspect)),
                        static_cast<vk::ImageLayout>(imageLayout),
                    },
            });

            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::SampledImage& info)
        {
            assert(info.texture && *info.texture);

            m_Bindings[index] = BindingInfo {
                .type         = vk::DescriptorType::eSampledImage,
                .count        = 1,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };
            addImage(info.texture->getImageView(toVk(info.imageAspect)),
                     static_cast<vk::ImageLayout>(info.texture->getImageLayout()));

            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageImage& info)
        {
            assert(info.texture && *info.texture);
            const auto imageLayout = info.texture->getImageLayout();
            assert(imageLayout == ImageLayout::eGeneral);

            const auto numImages = info.mipLevel ? 1 : info.texture->getNumMipLevels();

            m_Bindings[index] = BindingInfo {
                .type         = vk::DescriptorType::eStorageImage,
                .count        = numImages,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };
            for (auto i = 0u; i < numImages; ++i)
            {
                addImage(info.texture->getMipLevel(i, toVk(info.imageAspect)),
                         static_cast<vk::ImageLayout>(imageLayout));
            }

            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::UniformBuffer& info)
        {
            assert(info.buffer && validRange(*info.buffer, info.offset, info.range));
            return bindBuffer(index,
                              vk::DescriptorType::eUniformBuffer,
                              vk::DescriptorBufferInfo {
                                  info.buffer->getHandle(),
                                  info.offset,
                                  info.range.value_or(vk::WholeSize),
                              });
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageBuffer& info)
        {
            assert(info.buffer && validRange(*info.buffer, info.offset, info.range));
            return bindBuffer(index,
                              vk::DescriptorType::eStorageBuffer,
                              vk::DescriptorBufferInfo {
                                  info.buffer->getHandle(),
                                  info.offset,
                                  info.range.value_or(vk::WholeSize),
                              });
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex                        index,
                                                         const bindings::AccelerationStructureKHR& info)
        {
            assert(info.as);
            m_Bindings[index] = BindingInfo {
                .type         = vk::DescriptorType::eAccelerationStructureKHR,
                .count        = 1,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };
            addAccelerationStructure(info.as->getHandle());

            return *this;
        }

        vk::DescriptorSet DescriptorSetBuilder::build(const vk::DescriptorSetLayout layout)
        {
            assert(layout);

            auto hash = std::bit_cast<std::size_t>(layout);

            std::vector<vk::WriteDescriptorSet> writeDescriptors;
            writeDescriptors.reserve(m_Bindings.size());
            for (const auto& [index, binding] : m_Bindings)
            {
                hashCombine(hash, index, binding.type);

                vk::WriteDescriptorSet record {};
                record.dstBinding      = index;
                record.descriptorCount = binding.count;
                record.descriptorType  = binding.type;

                void* descriptorPtr = &m_Descriptors[binding.descriptorId];
                switch (binding.type)
                {
                    case vk::DescriptorType::eSampler:
                    case vk::DescriptorType::eCombinedImageSampler:
                    case vk::DescriptorType::eSampledImage:
                    case vk::DescriptorType::eStorageImage:
                        record.pImageInfo = std::bit_cast<const vk::DescriptorImageInfo*>(descriptorPtr);
                        hashCombine(hash, record.pImageInfo->imageView);
                        break;

                    case vk::DescriptorType::eUniformBuffer:
                    case vk::DescriptorType::eStorageBuffer:
                        record.pBufferInfo = std::bit_cast<const vk::DescriptorBufferInfo*>(descriptorPtr);
                        hashCombine(
                            hash, record.pBufferInfo->offset, record.pBufferInfo->range, record.pBufferInfo->buffer);
                        break;

                    case vk::DescriptorType::eAccelerationStructureKHR:
                        record.pNext =
                            std::bit_cast<const vk::WriteDescriptorSetAccelerationStructureKHR*>(descriptorPtr);
                        hashCombine(
                            hash,
                            reinterpret_cast<const vk::WriteDescriptorSetAccelerationStructureKHR*>(record.pNext)
                                ->pAccelerationStructures);
                        break;

                    default:
                        assert(false);
                }
                writeDescriptors.emplace_back(std::move(record));
            }

            vk::DescriptorSet descriptorSet {nullptr};
            if (const auto it = m_DescriptorSetCache.find(hash); it != m_DescriptorSetCache.cend())
            {
                descriptorSet = it->second;
            }
            else
            {
                descriptorSet = m_DescriptorSetAllocator.allocate(layout);
                for (auto& record : writeDescriptors)
                    record.dstSet = descriptorSet;

                m_Device.updateDescriptorSets(
                    static_cast<uint32_t>(writeDescriptors.size()), writeDescriptors.data(), 0, nullptr);
                m_DescriptorSetCache.emplace(hash, descriptorSet);
            }
            clear();
            return descriptorSet;
        }

        void DescriptorSetBuilder::clear()
        {
            m_Bindings.clear();
            m_Descriptors.clear();
            m_AccelerationStructures.clear();
        }

        void DescriptorSetBuilder::addImage(const vk::ImageView imageView, const vk::ImageLayout imageLayout)
        {
            assert(imageView && imageLayout != vk::ImageLayout::eUndefined);
            m_Descriptors.emplace_back(DescriptorVariant {
                .imageInfo =
                    vk::DescriptorImageInfo {
                        nullptr,
                        imageView,
                        imageLayout,
                    },
            });
        }

        void DescriptorSetBuilder::addSampler(const vk::Sampler sampler)
        {
            assert(sampler);
            m_Descriptors.emplace_back(DescriptorVariant {
                .imageInfo =
                    vk::DescriptorImageInfo {
                        sampler,
                        nullptr,
                        vk::ImageLayout::eUndefined,
                    },
            });
        }

        void DescriptorSetBuilder::addCombinedImageSampler(const vk::ImageView   imageView,
                                                           const vk::ImageLayout imageLayout,
                                                           const vk::Sampler     sampler)
        {
            assert(imageView && imageLayout != vk::ImageLayout::eUndefined && sampler);
            m_Descriptors.emplace_back(DescriptorVariant {
                .imageInfo =
                    vk::DescriptorImageInfo {
                        sampler,
                        imageView,
                        imageLayout,
                    },
            });
        }

        void DescriptorSetBuilder::addAccelerationStructure(const vk::AccelerationStructureKHR& as)
        {
            assert(as);

            m_AccelerationStructures.push_back(as); // Keep alive

            vk::WriteDescriptorSetAccelerationStructureKHR descriptorASInfo {};
            descriptorASInfo.accelerationStructureCount = 1;
            descriptorASInfo.pAccelerationStructures    = &m_AccelerationStructures.back();
            m_Descriptors.emplace_back(DescriptorVariant {
                .asInfo = descriptorASInfo,
            });
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bindBuffer(const BindingIndex         index,
                                                               const vk::DescriptorType   type,
                                                               vk::DescriptorBufferInfo&& bufferInfo)
        {
            m_Bindings[index] = BindingInfo {
                .type         = type,
                .count        = 1,
                .descriptorId = static_cast<int32_t>(m_Descriptors.size()),
            };
            m_Descriptors.emplace_back(DescriptorVariant {
                .bufferInfo = std::move(bufferInfo),
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