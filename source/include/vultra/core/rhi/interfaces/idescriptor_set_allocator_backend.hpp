#pragma once

#include "vultra/core/rhi/structs/handles.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IDescriptorSetAllocatorBackend
        {
        public:
            virtual ~IDescriptorSetAllocatorBackend() = default;

            [[nodiscard]] virtual std::uintptr_t createPool(uint32_t setsPerPool, bool raytracing) = 0;
            virtual void                         resetPool(std::uintptr_t poolHandle)                = 0;
            virtual void                         destroyPool(std::uintptr_t poolHandle)              = 0;

            [[nodiscard]] virtual DescriptorSetHandle
            allocateDescriptorSet(std::uintptr_t poolHandle, std::uintptr_t descriptorSetLayoutHandle, uint32_t variableDescriptorCount) = 0;
        };
    } // namespace rhi
} // namespace vultra

