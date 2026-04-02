#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        // Backend-agnostic descriptor layout/binding flags used by RHI metadata.
        // Values are stable API values; backend maps are done in concrete implementations.
        namespace descriptor_layout_flags
        {
            constexpr uint32_t eNone = 0u;
            constexpr uint32_t eUpdateAfterBindPool = 0x00000002u;
            constexpr uint32_t eVariableDescriptorCount = 0x00000004u;
        } // namespace descriptor_layout_flags
    } // namespace rhi
} // namespace vultra
