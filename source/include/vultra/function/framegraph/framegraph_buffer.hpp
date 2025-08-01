#pragma once

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
        };

        class FrameGraphBuffer
        {
        public:
            struct Desc
            {
                BufferType type;
                uint32_t   stride {sizeof(std::byte)};
                uint64_t   capacity;

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