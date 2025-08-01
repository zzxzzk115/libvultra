#include "vultra/core/rhi/cull_mode.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        std::string_view toString(const CullMode cullMode) { return magic_enum::enum_name(cullMode); }
    } // namespace rhi
} // namespace vultra