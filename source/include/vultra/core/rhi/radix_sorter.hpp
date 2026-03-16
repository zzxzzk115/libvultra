#pragma once

#include <cstdint>
#include <memory>

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class Buffer;
        class CommandBuffer;
        class RenderDevice;

        struct RadixSorterStorageRequirements
        {
            vk::DeviceSize       size {0};
            vk::BufferUsageFlags usage;
        };

        class RadixSorter final
        {
            friend class RenderDevice;

        public:
            RadixSorter()                   = default;
            RadixSorter(const RadixSorter&) = delete;
            RadixSorter(RadixSorter&&) noexcept;
            ~RadixSorter();

            RadixSorter& operator=(const RadixSorter&) = delete;
            RadixSorter& operator=(RadixSorter&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] uint32_t                       getMaxElementCount() const;
            [[nodiscard]] RadixSorterStorageRequirements getStorageRequirements() const;
            [[nodiscard]] RadixSorterStorageRequirements getKeyValueStorageRequirements() const;

            void sortKeys(CommandBuffer&,
                          uint32_t       elementCount,
                          const Buffer&  keys,
                          vk::DeviceSize keysOffset,
                          const Buffer&  storage,
                          vk::DeviceSize storageOffset) const;

            void sortKeyValues(CommandBuffer&,
                               uint32_t       elementCount,
                               const Buffer&  keys,
                               vk::DeviceSize keysOffset,
                               const Buffer&  values,
                               vk::DeviceSize valuesOffset,
                               const Buffer&  storage,
                               vk::DeviceSize storageOffset) const;

            void sortKeyValuesIndirect(CommandBuffer&,
                                       uint32_t       maxElementCount,
                                       const Buffer&  indirect,
                                       vk::DeviceSize indirectOffset,
                                       const Buffer&  keys,
                                       vk::DeviceSize keysOffset,
                                       const Buffer&  values,
                                       vk::DeviceSize valuesOffset,
                                       const Buffer&  storage,
                                       vk::DeviceSize storageOffset) const;

        private:
            struct Impl;

            explicit RadixSorter(std::unique_ptr<Impl>&&);
            static RadixSorter create(RenderDevice&, uint32_t maxElementCount);

        private:
            std::unique_ptr<Impl> m_Impl;
        };
    } // namespace rhi
} // namespace vultra
