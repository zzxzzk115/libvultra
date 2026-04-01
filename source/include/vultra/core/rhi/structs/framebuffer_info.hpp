#pragma once

#include "vultra/core/rhi/structs/cube_face.hpp"
#include "vultra/core/rhi/structs/rect2d.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"

#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/ext/vector_uint4.hpp>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;

        using ClearValue = std::variant<glm::vec4, glm::ivec4, glm::uvec4, float, uint32_t>;

        struct AttachmentInfo
        {
            Texture*                     target {nullptr};
            std::optional<uint32_t>      layer {};
            std::optional<CubeFace>      face {};
            std::optional<ClearValue>    clearValue {};
        };

        struct FramebufferInfo
        {
            Rect2D                       area {};
            uint32_t                     layers {1};
            uint32_t                     viewMask {0};
            std::vector<AttachmentInfo>  colorAttachments {};
            std::optional<AttachmentInfo> depthAttachment {};
            std::optional<AttachmentInfo> stencilAttachment {};
            bool                         depthReadOnly {false};
            bool                         stencilReadOnly {false};
        };

        [[nodiscard]] PixelFormat getDepthFormat(const FramebufferInfo&);
        [[nodiscard]] PixelFormat getColorFormat(const FramebufferInfo&, AttachmentIndex index);
        [[nodiscard]] std::vector<PixelFormat> getColorFormats(const FramebufferInfo&);
    } // namespace rhi
} // namespace vultra
