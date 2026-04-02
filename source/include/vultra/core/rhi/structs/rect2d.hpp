#pragma once

#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/offset2d.hpp"

#include <glm/ext/vector_int4.hpp>

namespace vultra
{
    namespace rhi
    {
        struct Rect2D
        {
            Offset2D offset;
            Extent2D extent;

            [[nodiscard]] explicit operator glm::ivec4() const
            {
                return {offset.x, offset.y, extent.width, extent.height};
            }

            auto operator<=>(const Rect2D&) const = default;

            template<class Archive>
            void serialize(Archive& archive)
            {
                archive(offset, extent);
            }
        };
    } // namespace rhi
} // namespace vultra
