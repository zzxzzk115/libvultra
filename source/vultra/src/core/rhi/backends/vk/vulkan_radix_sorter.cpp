#include "vultra/core/rhi/backends/vk/vulkan_radix_sorter.hpp"

#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <vk_radix_sort.h>

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] BufferUsage toRhi(const VkBufferUsageFlags usage)
            {
                BufferUsage out {BufferUsage::eNone};
                if (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                    out |= BufferUsage::eTransferSrc;
                if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                    out |= BufferUsage::eTransferDst;
                if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
                    out |= BufferUsage::eVertexBuffer;
                if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                    out |= BufferUsage::eIndexBuffer;
                if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
                    out |= BufferUsage::eUniformBuffer;
                if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
                    out |= BufferUsage::eStorageBuffer;
                if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
                    out |= BufferUsage::eIndirectBuffer;
                if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                    out |= BufferUsage::eShaderDeviceAddress;
                if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
                    out |= BufferUsage::eAccelerationBuildInput;
                if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
                    out |= BufferUsage::eAccelerationStorage;
                if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
                    out |= BufferUsage::eShaderBindingTable;
                return out;
            }
        } // namespace

        VulkanRadixSorter::VulkanRadixSorter(const RenderDevice& rd, const uint32_t maxElementCount) :
            m_MaxElementCount(maxElementCount)
        {
            assert(maxElementCount > 0u);

            VrdxSorterCreateInfo createInfo {};
            createInfo.physicalDevice =
                asVkHandle<VkPhysicalDevice>(VulkanRenderDeviceAccess::getPhysicalDeviceHandle(rd));
            createInfo.device = asVkHandle<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd));
            createInfo.pipelineCache =
                asVkHandle<VkPipelineCache>(VulkanRenderDeviceAccess::getPipelineCacheHandle(rd));

            vrdxCreateSorter(&createInfo, &m_Sorter);
            if (m_Sorter)
            {
                VrdxSorterStorageRequirements storageRequirements {};
                vrdxGetSorterStorageRequirements(m_Sorter, maxElementCount, &storageRequirements);
                m_StorageRequirements = {.size = storageRequirements.size, .usage = toRhi(storageRequirements.usage)};

                VrdxSorterStorageRequirements keyValueStorageRequirements {};
                vrdxGetSorterKeyValueStorageRequirements(m_Sorter, maxElementCount, &keyValueStorageRequirements);
                m_KeyValueStorageRequirements = {.size  = keyValueStorageRequirements.size,
                                                 .usage = toRhi(keyValueStorageRequirements.usage)};
            }
        }

        VulkanRadixSorter::~VulkanRadixSorter()
        {
            if (m_Sorter)
            {
                vrdxDestroySorter(m_Sorter);
                m_Sorter = nullptr;
            }
        }

        VulkanRadixSorter::operator bool() const { return m_Sorter != nullptr; }

        uint32_t VulkanRadixSorter::getMaxElementCount() const { return m_MaxElementCount; }

        RadixSorterStorageRequirements VulkanRadixSorter::getStorageRequirements() const
        {
            return m_StorageRequirements;
        }

        RadixSorterStorageRequirements VulkanRadixSorter::getKeyValueStorageRequirements() const
        {
            return m_KeyValueStorageRequirements;
        }

        void VulkanRadixSorter::sortKeys(CommandBuffer& cb,
                                         const uint32_t elementCount,
                                         const Buffer&  keys,
                                         const uint64_t keysOffset,
                                         const Buffer&  storage,
                                         const uint64_t storageOffset) const
        {
            if (elementCount <= 1u)
                return;

            cb.insertComputeUavBarrier();
            vrdxCmdSort(asVkHandle<VkCommandBuffer>(cb.getHandle()),
                        m_Sorter,
                        elementCount,
                        asVkHandle<VkBuffer>(keys.getHandle()),
                        keysOffset,
                        asVkHandle<VkBuffer>(storage.getHandle()),
                        storageOffset,
                        VK_NULL_HANDLE,
                        0u);
            cb.insertComputeUavBarrier();
        }

        void VulkanRadixSorter::sortKeyValues(CommandBuffer& cb,
                                              const uint32_t elementCount,
                                              const Buffer&  keys,
                                              const uint64_t keysOffset,
                                              const Buffer&  values,
                                              const uint64_t valuesOffset,
                                              const Buffer&  storage,
                                              const uint64_t storageOffset) const
        {
            if (elementCount <= 1u)
                return;

            cb.insertComputeUavBarrier();
            vrdxCmdSortKeyValue(asVkHandle<VkCommandBuffer>(cb.getHandle()),
                                m_Sorter,
                                elementCount,
                                asVkHandle<VkBuffer>(keys.getHandle()),
                                keysOffset,
                                asVkHandle<VkBuffer>(values.getHandle()),
                                valuesOffset,
                                asVkHandle<VkBuffer>(storage.getHandle()),
                                storageOffset,
                                VK_NULL_HANDLE,
                                0u);
            cb.insertComputeUavBarrier();
        }

        void VulkanRadixSorter::sortKeyValuesIndirect(CommandBuffer& cb,
                                                      const uint32_t maxElementCount,
                                                      const Buffer&  indirect,
                                                      const uint64_t indirectOffset,
                                                      const Buffer&  keys,
                                                      const uint64_t keysOffset,
                                                      const Buffer&  values,
                                                      const uint64_t valuesOffset,
                                                      const Buffer&  storage,
                                                      const uint64_t storageOffset) const
        {
            if (maxElementCount <= 1u)
                return;

            cb.insertComputeUavBarrier();
            vrdxCmdSortKeyValueIndirect(asVkHandle<VkCommandBuffer>(cb.getHandle()),
                                        m_Sorter,
                                        maxElementCount,
                                        asVkHandle<VkBuffer>(indirect.getHandle()),
                                        indirectOffset,
                                        asVkHandle<VkBuffer>(keys.getHandle()),
                                        keysOffset,
                                        asVkHandle<VkBuffer>(values.getHandle()),
                                        valuesOffset,
                                        asVkHandle<VkBuffer>(storage.getHandle()),
                                        storageOffset,
                                        VK_NULL_HANDLE,
                                        0u);
            cb.insertComputeUavBarrier();
        }
    } // namespace rhi
} // namespace vultra
