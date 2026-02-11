#pragma once

#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/renderer/mesh_resource.hpp"

#include <vasset/vasset.hpp>

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
        // -------- Texture loaders --------

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

        // -------- Material loaders (VMaterial only) --------
        [[nodiscard]] std::expected<vasset::VMaterial, std::string>
        loadMaterial_VMaterial(const std::filesystem::path&);

        // -------- Mesh loaders --------
        [[nodiscard]] std::expected<gfx::DefaultMesh, std::string> loadMesh_VMesh(const std::filesystem::path&,
                                                                                  rhi::RenderDevice&);

        [[nodiscard]] std::expected<gfx::DefaultMesh, std::string> loadMesh_Raw(const std::filesystem::path&,
                                                                                rhi::RenderDevice&);
    } // namespace resource
} // namespace vultra