#pragma once

#include "vultra/core/rhi/interfaces/itexture.hpp"

namespace vultra
{
    namespace rhi
    {
        class VulkanTexture : public ITexture
        {
        public:
            virtual ~VulkanTexture() = default;
        };
    } // namespace rhi
} // namespace vultra
