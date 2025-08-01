#include "vultra/core/rhi/image_usage.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        std::string_view toString(ImageUsage imageUsage) { return magic_enum::enum_name(imageUsage); }
    } // namespace rhi
} // namespace vultra