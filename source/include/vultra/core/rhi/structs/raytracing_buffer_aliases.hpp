#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class AccelerationStructureBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            AccelerationStructureBuffer() = default;

            [[nodiscard]] uint64_t getCapacity() const { return getSize(); }

        private:
            explicit AccelerationStructureBuffer(Buffer&& buffer) : Buffer(std::move(buffer)) {}
        };

        class TransformBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            TransformBuffer() = default;

            [[nodiscard]] uint64_t getCapacity() const { return getSize(); }

        private:
            explicit TransformBuffer(Buffer&& buffer) : Buffer(std::move(buffer)) {}
        };

        class InstanceBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            InstanceBuffer() = default;

            [[nodiscard]] uint64_t getCapacity() const { return m_InstanceCount; }

        private:
            InstanceBuffer(Buffer&& buffer, uint32_t instanceCount) :
                Buffer(std::move(buffer)), m_InstanceCount(instanceCount)
            {}

            uint32_t m_InstanceCount {0u};
        };
    } // namespace rhi
} // namespace vultra
