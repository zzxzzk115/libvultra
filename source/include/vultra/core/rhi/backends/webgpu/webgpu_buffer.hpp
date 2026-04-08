#pragma once

#include "vultra/core/rhi/interfaces/ibuffer.hpp"

#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class IRenderDevice;

        class WebGPUBuffer final : public IBuffer
        {
        public:
            WebGPUBuffer(IRenderDevice* renderDevice, uint64_t size, std::uintptr_t handle, std::uintptr_t queueHandle);
            ~WebGPUBuffer() override;

            [[nodiscard]] bool           isValid() const override { return m_Valid; }
            [[nodiscard]] std::uintptr_t getHandle() const override { return m_Handle; }
            [[nodiscard]] uint64_t       getSize() const override { return m_Size; }
            [[nodiscard]] BarrierScope   getLastScope() const override { return m_LastScope; }
            void                         setLastScope(BarrierScope scope) override { m_LastScope = scope; }

            void* map() override;
            void  unmap() override;
            void  flush(uint64_t offset, uint64_t size) override;

        private:
            IRenderDevice*         m_RenderDevice {nullptr};
            std::vector<std::byte> m_Data;
            uint64_t               m_Size {0};
            std::uintptr_t         m_Handle {0};
            std::uintptr_t         m_QueueHandle {0};
            bool                   m_Valid {false};
            bool                   m_Mapped {false};
            BarrierScope           m_LastScope {};
        };
    } // namespace rhi
} // namespace vultra
