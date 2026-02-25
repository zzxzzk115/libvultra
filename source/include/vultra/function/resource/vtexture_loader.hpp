#pragma once

#include "vultra/core/rhi/texture.hpp"

#include <vasset/vtexture.hpp>
#include <vbase/core/result.hpp>

#include <string>

namespace vultra::resource
{
    vbase::Result<rhi::Texture, std::string> loadTextureFromVTexture(const vasset::VTexture& vtexture,
                                                                     rhi::RenderDevice&      rd);

}