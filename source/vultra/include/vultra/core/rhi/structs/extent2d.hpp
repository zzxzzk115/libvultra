#pragma once

#include <glm/ext/vector_uint2.hpp>

#include <compare>

namespace vultra
{
    namespace rhi
    {
        struct Extent2D
        {
            uint32_t width {0};
            uint32_t height {0};

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] explicit operator glm::uvec2() const;

            [[nodiscard]] float getAspectRatio() const;

            auto operator<=>(const Extent2D&) const = default;

            template<class Archive>
            void serialize(Archive& archive)
            {
                archive(width, height);
            }
        };
    } // namespace rhi
} // namespace vultra
