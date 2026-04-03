#include "vultra/core/rhi/backends/webgpu/webgpu_descriptor_set.hpp"

#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <algorithm>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] constexpr ImageAspectFlags toImageAspectFlags(const ImageAspect aspect)
            {
                switch (aspect)
                {
                    case ImageAspect::eColor:
                        return ImageAspectFlags::eColor;
                    case ImageAspect::eDepth:
                        return ImageAspectFlags::eDepth;
                    case ImageAspect::eStencil:
                        return ImageAspectFlags::eStencil;
                    case ImageAspect::eNone:
                    default:
                        return ImageAspectFlags::eNone;
                }
            }

            [[nodiscard]] std::size_t hashSamplerInfo(const SamplerInfo& info)
            {
                std::size_t hash {0};
                hashCombine(hash,
                            info.magFilter,
                            info.minFilter,
                            info.mipmapMode,
                            info.addressModeS,
                            info.addressModeT,
                            info.addressModeR,
                            info.compareOp,
                            info.minLod,
                            info.maxLod,
                            info.borderColor);
                if (info.maxAnisotropy.has_value())
                {
                    hashCombine(hash, *info.maxAnisotropy);
                }
                else
                {
                    hashCombine(hash, 0.0f);
                }
                return hash;
            }

            [[nodiscard]] SamplerHandle getOrCreateSamplerHandle(const WebGPURenderDevice& backend,
                                                                 const Sampler&            sampler)
            {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
                (void)backend;
                (void)sampler;
                return {};
#else
                auto& mutableBackend = const_cast<WebGPURenderDevice&>(backend);
                if (mutableBackend.m_Device == nullptr)
                {
                    return {};
                }

                const auto hash = hashSamplerInfo(sampler.info());
                if (auto it = mutableBackend.m_Samplers.find(hash); it != mutableBackend.m_Samplers.end())
                {
                    return it->second;
                }

                WGPUSamplerDescriptor descriptor {};
                descriptor.magFilter       = webgpu::toWgpuFilter(sampler.info().magFilter);
                descriptor.minFilter       = webgpu::toWgpuFilter(sampler.info().minFilter);
                descriptor.mipmapFilter    = webgpu::toWgpuMipmapFilter(sampler.info().mipmapMode);
                descriptor.addressModeU    = webgpu::toWgpuAddressMode(sampler.info().addressModeS);
                descriptor.addressModeV    = webgpu::toWgpuAddressMode(sampler.info().addressModeT);
                descriptor.addressModeW    = webgpu::toWgpuAddressMode(sampler.info().addressModeR);
                descriptor.lodMinClamp     = sampler.info().minLod;
                descriptor.lodMaxClamp     = sampler.info().maxLod;
                const bool allLinearFilter = descriptor.magFilter == WGPUFilterMode_Linear &&
                                             descriptor.minFilter == WGPUFilterMode_Linear &&
                                             descriptor.mipmapFilter == WGPUMipmapFilterMode_Linear;
                descriptor.maxAnisotropy =
                    (allLinearFilter && sampler.info().maxAnisotropy.has_value()) ?
                        static_cast<uint16_t>(std::clamp(*sampler.info().maxAnisotropy, 1.0f, 16.0f)) :
                        1u;
                descriptor.compare = WGPUCompareFunction_Undefined;

                auto* const wgpuSampler = wgpuDeviceCreateSampler(mutableBackend.m_Device, &descriptor);
                if (wgpuSampler == nullptr)
                {
                    return {};
                }

                const auto inserted =
                    mutableBackend.m_Samplers.emplace(hash,
                                                      SamplerHandle {
                                                          reinterpret_cast<std::uintptr_t>(wgpuSampler),
                                                      });
                return inserted.first->second;
#endif
            }
        } // namespace

        WebGPUDescriptorSet::WebGPUDescriptorSet(const DescriptorSetLayoutKey                      layoutKey,
                                                 std::unordered_map<BindingIndex, ResourceBinding> bindings) :
            m_LayoutKey(layoutKey), m_Bindings(std::move(bindings))
        {}

        WebGPUDescriptorSet::~WebGPUDescriptorSet()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            for (const auto& [_, bindGroup] : m_BindGroups)
            {
                if (bindGroup != nullptr)
                {
                    wgpuBindGroupRelease(bindGroup);
                }
            }
#endif
            m_BindGroups.clear();
        }

        WGPUBindGroup WebGPUDescriptorSet::getOrCreateBindGroup(const WebGPURenderDevice&    backend,
                                                                const DescriptorSetLayoutKey expectedLayoutKey)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)backend;
            (void)expectedLayoutKey;
            return nullptr;
#else
            if (!expectedLayoutKey)
            {
                return nullptr;
            }

            if (const auto it = m_BindGroups.find(expectedLayoutKey.value); it != m_BindGroups.end())
            {
                return it->second;
            }

            const auto bindGroupLayoutIt = backend.m_DescriptorSetLayouts.find(expectedLayoutKey.value);
            if (bindGroupLayoutIt == backend.m_DescriptorSetLayouts.end())
            {
                return nullptr;
            }
            const auto layoutBindingsIt = backend.m_DescriptorSetLayoutBindings.find(expectedLayoutKey.value);
            if (layoutBindingsIt == backend.m_DescriptorSetLayoutBindings.end())
            {
                return nullptr;
            }

            std::vector<WGPUBindGroupEntry> entries;
            entries.reserve(layoutBindingsIt->second.size() * 2u);

            for (const auto& layoutBinding : layoutBindingsIt->second)
            {
                const auto bindingIndex = layoutBinding.binding;
                const auto resourceIt   = m_Bindings.find(bindingIndex);
                if (resourceIt == m_Bindings.end())
                {
                    continue;
                }
                const auto& resourceBinding = resourceIt->second;

                switch (layoutBinding.type)
                {
                    case DescriptorType::eUniformBuffer: {
                        const auto* value = std::get_if<bindings::UniformBuffer>(&resourceBinding);
                        if (value == nullptr || value->buffer == nullptr || value->buffer->getHandle() == 0)
                        {
                            break;
                        }
                        const auto bufferSize = value->buffer->getSize();
                        if (value->offset >= bufferSize)
                        {
                            break;
                        }
                        WGPUBindGroupEntry entry {};
                        entry.binding = bindingIndex;
                        entry.buffer  = reinterpret_cast<WGPUBuffer>(value->buffer->getHandle());
                        entry.offset  = value->offset;
                        entry.size    = value->range.value_or(bufferSize - value->offset);
                        entries.push_back(entry);
                        break;
                    }
                    case DescriptorType::eStorageBuffer:
                    case DescriptorType::eStorageBufferDynamic: {
                        const auto* value = std::get_if<bindings::StorageBuffer>(&resourceBinding);
                        if (value == nullptr || value->buffer == nullptr || value->buffer->getHandle() == 0)
                        {
                            break;
                        }
                        const auto bufferSize = value->buffer->getSize();
                        if (value->offset >= bufferSize)
                        {
                            break;
                        }
                        WGPUBindGroupEntry entry {};
                        entry.binding = bindingIndex;
                        entry.buffer  = reinterpret_cast<WGPUBuffer>(value->buffer->getHandle());
                        entry.offset  = value->offset;
                        entry.size    = value->range.value_or(bufferSize - value->offset);
                        entries.push_back(entry);
                        break;
                    }
                    case DescriptorType::eCombinedImageSampler: {
                        const auto* value = std::get_if<bindings::CombinedImageSampler>(&resourceBinding);
                        if (value == nullptr || value->texture == nullptr)
                        {
                            break;
                        }
                        const auto imageView =
                            value->texture->getImageView(toImageAspectFlags(value->imageAspect)).getHandle();
                        if (imageView == 0)
                        {
                            break;
                        }

                        const auto sampler =
                            getOrCreateSamplerHandle(backend, value->sampler.value_or(value->texture->getSampler()));
                        if (!sampler)
                        {
                            break;
                        }

                        WGPUBindGroupEntry textureEntry {};
                        textureEntry.binding     = bindingIndex;
                        textureEntry.textureView = reinterpret_cast<WGPUTextureView>(imageView);
                        entries.push_back(textureEntry);

                        WGPUBindGroupEntry samplerEntry {};
                        samplerEntry.binding = bindingIndex + 1u;
                        samplerEntry.sampler = reinterpret_cast<WGPUSampler>(sampler.value);
                        entries.push_back(samplerEntry);
                        break;
                    }
                    case DescriptorType::eSampledImage: {
                        const auto* value = std::get_if<bindings::SampledImage>(&resourceBinding);
                        if (value == nullptr || value->texture == nullptr)
                        {
                            break;
                        }
                        const auto imageView =
                            value->texture->getImageView(toImageAspectFlags(value->imageAspect)).getHandle();
                        if (imageView == 0)
                        {
                            break;
                        }
                        WGPUBindGroupEntry entry {};
                        entry.binding     = bindingIndex;
                        entry.textureView = reinterpret_cast<WGPUTextureView>(imageView);
                        entries.push_back(entry);
                        break;
                    }
                    case DescriptorType::eSampler: {
                        const auto* value = std::get_if<bindings::SeparateSampler>(&resourceBinding);
                        if (value == nullptr)
                        {
                            break;
                        }
                        const auto sampler = getOrCreateSamplerHandle(backend, value->handle);
                        if (!sampler)
                        {
                            break;
                        }
                        WGPUBindGroupEntry entry {};
                        entry.binding = bindingIndex;
                        entry.sampler = reinterpret_cast<WGPUSampler>(sampler.value);
                        entries.push_back(entry);
                        break;
                    }
                    case DescriptorType::eStorageImage: {
                        const auto* value = std::get_if<bindings::StorageImage>(&resourceBinding);
                        if (value == nullptr || value->texture == nullptr)
                        {
                            break;
                        }
                        const auto imageView =
                            value->texture->getImageView(toImageAspectFlags(value->imageAspect)).getHandle();
                        if (imageView == 0)
                        {
                            break;
                        }
                        WGPUBindGroupEntry entry {};
                        entry.binding     = bindingIndex;
                        entry.textureView = reinterpret_cast<WGPUTextureView>(imageView);
                        entries.push_back(entry);
                        break;
                    }
                    case DescriptorType::eInputAttachment:
                    case DescriptorType::eAccelerationStructure:
                        break;
                }
            }

            if (entries.empty())
            {
                return nullptr;
            }

            WGPUBindGroupDescriptor descriptor {};
            descriptor.layout     = bindGroupLayoutIt->second;
            descriptor.entryCount = entries.size();
            descriptor.entries    = entries.data();
            auto* const bindGroup = wgpuDeviceCreateBindGroup(backend.m_Device, &descriptor);
            if (bindGroup == nullptr)
            {
                return nullptr;
            }
            m_BindGroups.emplace(expectedLayoutKey.value, bindGroup);
            return bindGroup;
#endif
        }
    } // namespace rhi
} // namespace vultra
