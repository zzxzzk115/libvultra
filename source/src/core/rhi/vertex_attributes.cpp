#include "vultra/core/rhi/vertex_attributes.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        uint32_t getSize(const VertexAttribute::Type type)
        {
            switch (type)
            {
                using enum VertexAttribute::Type;
                case eFloat:
                    return sizeof(float);
                case eFloat2:
                    return sizeof(float) * 2;
                case eFloat3:
                    return sizeof(float) * 3;
                case eFloat4:
                    return sizeof(float) * 4;

                case eInt4:
                    return sizeof(int32_t) * 4;

                case eUByte4_Norm:
                    return sizeof(uint8_t) * 4;
            }

            assert(false);
            return 0;
        }
    } // namespace rhi
} // namespace vultra