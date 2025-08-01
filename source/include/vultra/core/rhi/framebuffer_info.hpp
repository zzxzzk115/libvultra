#pragma once

#include "vultra/core/rhi/cube_face.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/core/rhi/rect2d.hpp"
#include "vultra/core/rhi/resource_indices.hpp"

#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/ext/vector_uint4.hpp>

#include <optional>
#include <variant>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        using ClearValue = std::variant<glm::vec4, glm::ivec4, glm::uvec4, float, uint32_t>;

        class Texture;

        struct AttachmentInfo
        {
            Texture*                  target {nullptr};
            std::optional<uint32_t>   layer;
            std::optional<CubeFace>   face;
            std::optional<ClearValue> clearValue;
        };

        struct FramebufferInfo
        {
            Rect2D                        area;
            uint32_t                      layers {1};
            std::optional<AttachmentInfo> depthAttachment {std::nullopt};
            bool                          depthReadOnly {false};
            std::optional<AttachmentInfo> stencilAttachment {std::nullopt};
            bool                          stencilReadOnly {false};
            std::vector<AttachmentInfo>   colorAttachments;
        };

        [[nodiscard]] PixelFormat getDepthFormat(const FramebufferInfo&);
        [[nodiscard]] PixelFormat getColorFormat(const FramebufferInfo&, const AttachmentIndex);

        [[nodiscard]] std::vector<PixelFormat> getColorFormats(const FramebufferInfo&);
    } // namespace rhi
} // namespace vultra