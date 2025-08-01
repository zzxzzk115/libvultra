#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        struct AccelerationStructure
        {
        public:
            [[nodiscard]] vk::AccelerationStructureKHR getHandle() const;

        private:
            vk::AccelerationStructureKHR m_Handle {nullptr};
        };
    } // namespace rhi
} // namespace vultra