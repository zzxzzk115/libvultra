#pragma once

#include "vultra/core/rhi/texture.hpp"

#include <expected>
#include <filesystem>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    }

    namespace resource
    {
        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureSTB(const std::filesystem::path&,
                                                                              rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureKTX_DDS(const std::filesystem::path&,
                                                                                  rhi::RenderDevice&);
    } // namespace resource
} // namespace vultra