#include "vultra/core/rhi/extent2d.hpp"

#include <glm/ext/scalar_constants.hpp>

namespace vultra
{
    namespace rhi
    {
        Extent2D::operator bool() const { return width > 0 && height > 0; }

        Extent2D::operator vk::Extent2D() const { return {width, height}; }

        Extent2D::operator glm::vec<2, unsigned>() const { return {width, height}; }

        float Extent2D::getAspectRatio() const
        {
            return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : glm::epsilon<float>();
        }
    } // namespace rhi
} // namespace vultra