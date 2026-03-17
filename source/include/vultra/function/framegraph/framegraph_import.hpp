#pragma once

#include "vultra/function/framegraph/framegraph_buffer.hpp"

#include <fg/Fwd.hpp>

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        class Buffer;
        class Texture;
    } // namespace rhi

    namespace framegraph
    {
        [[nodiscard]] FrameGraphResource importTexture(FrameGraph&, const std::string_view name, rhi::Texture*);
        [[nodiscard]] FrameGraphResource importBuffer(FrameGraph&,
                                                      const std::string_view name,
                                                      rhi::Buffer*           buffer,
                                                      BufferType             type,
                                                      uint32_t               stride = 1u);
    } // namespace framegraph
} // namespace vultra