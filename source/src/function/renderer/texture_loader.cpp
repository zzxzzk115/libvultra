#include "vultra/function/renderer/texture_loader.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"

namespace vultra
{
    namespace gfx
    {
        namespace
        {
            [[nodiscard]] std::expected<rhi::Texture, std::string> tryLoad(const std::filesystem::path& p,
                                                                           rhi::RenderDevice&           rd)
            {
                if (!p.has_extension())
                {
                    return std::unexpected {"No extension."};
                }

                const auto ext = p.extension();

                switch (entt::hashed_string {ext.generic_string().c_str()})
                {
                    using namespace entt::literals;

                    case ".jpg"_hs:
                    case ".jpeg"_hs:
                    case ".png"_hs:
                    case ".tga"_hs:
                    case ".bmp"_hs:
                    case ".psd"_hs:
                    case ".gif"_hs:
                    case ".hdr"_hs:
                    case ".pic"_hs:
                        return resource::loadTextureSTB(p, rd);

                    case ".exr"_hs:
                        return resource::loadTextureEXR(p, rd);

                    case ".ktx"_hs:
                    case ".dds"_hs:
                        return resource::loadTextureKTX_DDS(p, rd);

                    case ".ktx2"_hs:
                        return resource::loadTextureKTX2(p, rd);

                    default:
                        break;
                }
                return std::unexpected {std::format("Unsupported extension: '{}'", ext.generic_string())};
            }

        } // namespace

        TextureLoader::result_type TextureLoader::operator()(const std::filesystem::path& p,
                                                             rhi::RenderDevice&           rd) const
        {
            if (auto texture = tryLoad(p, rd); texture)
            {
                return createRef<TextureResource>(std::move(texture.value()), p);
            }
            else
            {
                VULTRA_CORE_ERROR("[TextureLoader] Texture loading failed. {}", texture.error());
                return {};
            }
        }
        TextureLoader::result_type TextureLoader::operator()(rhi::Texture&& texture) const
        {
            return createRef<TextureResource>(std::move(texture), "");
        }
    } // namespace gfx
} // namespace vultra