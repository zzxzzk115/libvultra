#include "vultra/core/rhi/primitive_topology.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        std::string_view toString(const PrimitiveTopology primitiveTopology)
        {
            return magic_enum::enum_name(primitiveTopology);
        }
    } // namespace rhi
} // namespace vultra