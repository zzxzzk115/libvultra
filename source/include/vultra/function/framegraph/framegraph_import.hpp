#pragma once

#include <fg/Fwd.hpp>

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    } // namespace rhi

    namespace framegraph
    {
        [[nodiscard]] FrameGraphResource importTexture(FrameGraph&, const std::string_view name, rhi::Texture*);
    } // namespace framegraph
} // namespace vultra