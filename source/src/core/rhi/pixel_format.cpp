#include "vultra/core/rhi/pixel_format.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        uint8_t getBytesPerPixel(const PixelFormat pixelFormat)
        {
            switch (pixelFormat)
            {
                using enum rhi::PixelFormat;

                    //
                    // 8 bits per component:
                    //

                case eR8_UNorm:
                case eR8_SNorm:
                case eR8UI:
                case eR8I:
                    return 1;

                case eRG8_UNorm:
                case eRG8_SNorm:
                case eRG8UI:
                case eRG8I:
                    return 2;

                case eRGB8_UNorm:
                    return 3;

                case eRGBA8_UNorm:
                case eRGBA8_sRGB:
                case eBGRA8_UNorm:
                case eBGRA8_sRGB:
                case eRGBA8UI:
                case eRGBA8I:
                    return 4;

                    //
                    // 16 bits per component:
                    //

                case eR16_UNorm:
                case eR16_SNorm:
                case eR16F:
                case eR16UI:
                case eR16I:
                    return 2;

                case eRG16_UNorm:
                case eRG16_SNorm:
                case eRG16F:
                case eRG16UI:
                case eRG16I:
                    return 4;

                case eRGB16F:
                    return 6;

                case eRGBA16_UNorm:
                case eRGBA16_SNorm:
                case eRGBA16F:
                case eRGBA16UI:
                case eRGBA16I:
                    return 8;

                    //
                    // 32 bits per component:
                    //

                case eR32F:
                case eR32UI:
                case eR32I:
                    return 4;

                case eRG32F:
                case eRG32UI:
                case eRG32I:
                    return 8;

                case eRGBA32F:
                case eRGBA32UI:
                case eRGBA32I:
                    return 16;

                    //
                    // Depth/Stencil:
                    //

                case eDepth16:
                    return 2;
                case eDepth32F:
                    return 4;
                case eStencil8:
                    return 1;
                case eDepth16_Stencil8:
                    return 3;
                case eDepth24_Stencil8:
                    return 4;
                case eDepth32F_Stencil8:
                    return 5;

                default:
                    return 0;
            }
        }

        vk::ImageAspectFlags getAspectMask(const PixelFormat pixelFormat)
        {
            switch (pixelFormat)
            {
                using enum PixelFormat;

                case eUndefined:
                    return vk::ImageAspectFlagBits::eNone;

                case eDepth16:
                case eDepth32F:
                    return vk::ImageAspectFlagBits::eDepth;

                case eStencil8:
                    return vk::ImageAspectFlagBits::eStencil;

                case eDepth16_Stencil8:
                case eDepth24_Stencil8:
                case eDepth32F_Stencil8:
                    return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

                default:
                    return vk::ImageAspectFlagBits::eColor;
            }
        }

        std::string_view toString(const PixelFormat pixelFormat) { return magic_enum::enum_name(pixelFormat); }
    } // namespace rhi
} // namespace vultra