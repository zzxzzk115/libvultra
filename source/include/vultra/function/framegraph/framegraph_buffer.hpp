#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace vultra
{
    namespace rhi
    {
        class Buffer;
    }

    namespace framegraph
    {

        enum class BufferType
        {
            eUniformBuffer,
            eStorageBuffer,
            eVertexBuffer,
            eIndexBuffer,
            eDrawIndirectBuffer,
            eDispatchIndirectBuffer,
        };

        class FrameGraphBuffer
        {
        public:
            struct Desc
            {
                BufferType            type;
                uint32_t              stride {sizeof(std::byte)};
                uint64_t              capacity;
                rhi::BufferUsageFlags extraUsage {0};
                rhi::DrawIndirectType drawIndirectType {rhi::DrawIndirectType::eNonIndexed};

                [[nodiscard]] constexpr auto dataSize() const { return stride * capacity; }
            };

            void create(const Desc&, void* allocator);
            void destroy(const Desc&, void* allocator);

            void preRead(const Desc&, uint32_t flags, void* ctx);
            void preWrite(const Desc&, uint32_t flags, void* ctx);

            [[nodiscard]] static std::string toString(const Desc&);

            rhi::Buffer* buffer {nullptr};
        };
    } // namespace framegraph
} // namespace vultra
