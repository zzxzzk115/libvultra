#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"

namespace vultra
{
    namespace rhi
    {
        vk::AccelerationStructureKHR AccelerationStructure::getHandle() const { return m_Handle; }
    } // namespace rhi
} // namespace vultra