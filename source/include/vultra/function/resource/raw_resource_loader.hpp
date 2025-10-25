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
        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureVTexture(const std::filesystem::path&,
                                                                                   rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureSTB(const std::filesystem::path&,
                                                                              rhi::RenderDevice&);
        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureSTB_Raw(const std::vector<uint8_t>& bintex,
                                                                                  rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureEXR(const std::filesystem::path&,
                                                                              rhi::RenderDevice&);
        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureEXR_Raw(const std::vector<uint8_t>& bintex,
                                                                                  rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureKTX_DDS(const std::filesystem::path&,
                                                                                  rhi::RenderDevice&);
        [[nodiscard]] std::expected<rhi::Texture, std::string>
        loadTextureKTX_DDS_Raw(const std::vector<uint8_t>& bintex, rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureKTX2(const std::filesystem::path&,
                                                                               rhi::RenderDevice&);
        [[nodiscard]] std::expected<rhi::Texture, std::string> loadTextureKTX2_Raw(const std::vector<uint8_t>& bintex,
                                                                                   rhi::RenderDevice&);

        [[nodiscard]] std::expected<rhi::Texture, std::string>
        loadTextureRaw(const std::string& ext, const std::vector<uint8_t>& bintex, rhi::RenderDevice&);
    } // namespace resource
} // namespace vultra