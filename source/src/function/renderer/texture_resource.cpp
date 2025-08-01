#include "vultra/function/renderer/texture_resource.hpp"

namespace vultra
{
    namespace gfx
    {
        TextureResource::TextureResource(Texture&& texture, const std::filesystem::path& p) :
            Resource {p}, Texture {std::move(texture)}
        {}
    } // namespace gfx
} // namespace vultra