#pragma once

#include "vultra/core/rhi/structs/barrier_scope.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IBuffer
        {
        public:
            virtual ~IBuffer() = default;

            [[nodiscard]] virtual bool           isValid() const = 0;
            [[nodiscard]] virtual std::uintptr_t getHandle() const = 0;
            [[nodiscard]] virtual uint64_t       getSize() const = 0;
            [[nodiscard]] virtual BarrierScope   getLastScope() const = 0;
            virtual void                         setLastScope(BarrierScope) = 0;

            virtual void* map() = 0;
            virtual void  unmap() = 0;
            virtual void  flush(uint64_t offset, uint64_t size) = 0;
        };
    } // namespace rhi
} // namespace vultra

