#include "vultra/core/rhi/cube_face.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        std::string_view toString(const CubeFace cubeFace) { return magic_enum::enum_name(cubeFace); }
    } // namespace rhi
} // namespace vultra