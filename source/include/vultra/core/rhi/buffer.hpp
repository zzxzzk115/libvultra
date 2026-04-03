#pragma once

#include "vultra/core/rhi/structs/barrier_scope.hpp"
#include "vultra/core/rhi/interfaces/ibuffer.hpp"
#include "vultra/core/rhi/structs/buffer_usage.hpp"
#include "vultra/core/rhi/structs/buffer_structs.hpp"

#include <cstdint>
#include <memory>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Barrier;

        class Buffer
        {
            friend class RenderDevice;
            friend class Barrier;

        public:
            Buffer()              = default;
            Buffer(const Buffer&) = delete;
            Buffer(Buffer&&) noexcept;
            virtual ~Buffer();
            explicit Buffer(std::unique_ptr<IBuffer>);

            Buffer& operator=(const Buffer&) = delete;
            Buffer& operator=(Buffer&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            using Stride = uint32_t;

            [[nodiscard]] std::uintptr_t getHandle() const;
            [[nodiscard]] uint64_t       getSize() const;

            void*   map();
            Buffer& unmap();

            Buffer& flush(uint64_t offset = 0, uint64_t size = UINT64_MAX);

        private:
            void destroy() noexcept;
            [[nodiscard]] BarrierScope getBarrierScope() const;
            void                       setBarrierScope(BarrierScope);

        private:
            std::unique_ptr<IBuffer> m_Impl;
        };

    } // namespace rhi
} // namespace vultra
