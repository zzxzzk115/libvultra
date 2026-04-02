#pragma once

#include "vultra/core/rhi/structs/image_aspect.hpp"

#include <cstdint>
#include <string_view>

namespace vultra
{
    namespace rhi
    {
        enum class PixelFormat
        {
            eUndefined = 0,
            eR8_UNorm,
            eR8_SNorm,
            eR8UI,
            eR8I,
            eRG8_UNorm,
            eRG8_SNorm,
            eRG8UI,
            eRG8I,
            eRGB8_UNorm,
            eRGBA8_UNorm,
            eRGBA8_sRGB,
            eBGRA8_UNorm,
            eBGRA8_sRGB,
            eRGBA8UI,
            eRGBA8I,
            eBC1_UNorm,
            eBC2_UNorm,
            eBC3_UNorm,
            eBC4_UNorm,
            eBC5_UNorm,
            eBC6H_RGB16F,
            eBC7_RGBA8_UNorm,
            eR16_UNorm,
            eR16_SNorm,
            eR16F,
            eR16UI,
            eR16I,
            eRG16_UNorm,
            eRG16_SNorm,
            eRG16F,
            eRG16UI,
            eRG16I,
            eRGB16F,
            eRGBA16_UNorm,
            eRGBA16_SNorm,
            eRGBA16F,
            eRGBA16UI,
            eRGBA16I,
            eR32F,
            eR32UI,
            eR32I,
            eRG32F,
            eRG32UI,
            eRG32I,
            eRGBA32F,
            eRGBA32UI,
            eRGBA32I,
            eDepth16,
            eDepth32F,
            eStencil8,
            eDepth16_Stencil8,
            eDepth24_Stencil8,
            eDepth32F_Stencil8,
        };

        [[nodiscard]] uint8_t          getBytesPerPixel(PixelFormat);
        [[nodiscard]] ImageAspectFlags getAspectMask(PixelFormat);
        [[nodiscard]] std::string_view toString(PixelFormat);
    } // namespace rhi
} // namespace vultra
