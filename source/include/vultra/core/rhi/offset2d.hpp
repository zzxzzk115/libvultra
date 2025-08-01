#pragma once

#include <vulkan/vulkan.hpp>

#include <glm/ext/vector_int2.hpp>

namespace vultra
{
    namespace rhi
    {
        struct Offset2D
        {
            int32_t x {0};
            int32_t y {0};

            [[nodiscard]] explicit operator vk::Offset2D() const { return {x, y}; }
            [[nodiscard]] explicit operator glm::ivec2() const { return {x, y}; }

            auto operator<=>(const Offset2D&) const = default;

            template<class Archive>
            void serialize(Archive& archive)
            {
                archive(x, y);
            }
        };
    } // namespace rhi
} // namespace vultra