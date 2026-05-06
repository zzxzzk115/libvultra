#pragma once

#include "vultra/core/rhi/interfaces/itexture.hpp"

namespace vultra
{
    namespace rhi
    {
        class WebGPUTexture : public ITexture
        {
        public:
            virtual ~WebGPUTexture() = default;
        };
    } // namespace rhi
} // namespace vultra
