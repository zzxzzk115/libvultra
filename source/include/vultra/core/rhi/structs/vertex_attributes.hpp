#pragma once

#include <cstdint>
#include <map>

namespace vultra
{
    namespace rhi
    {
        struct VertexAttribute
        {
            enum class Type
            {
                eFloat,
                eFloat2,
                eFloat3,
                eFloat4,
                eInt4,
                eUByte4_Norm,
            };

            uint32_t location {0};
            Type     type {Type::eFloat};
            uint32_t offset {0};
        };

        using VertexAttributes = std::map<uint32_t, VertexAttribute>;

        [[nodiscard]] uint32_t getSize(VertexAttribute::Type);
    } // namespace rhi
} // namespace vultra
