#import <Metal/Metal.h>
#include <vri/vri.h>

#include <array>
#include <cstdint>
#include <utility>

namespace vultra
{
    std::array<std::pair<int64_t, VriFormat>, 4> metalXrFormats()
    {
        return {{{MTLPixelFormatRGBA8Unorm_sRGB, VriFormat_RGBA8_SRGB},
                 {MTLPixelFormatBGRA8Unorm_sRGB, VriFormat_BGRA8_SRGB},
                 {MTLPixelFormatRGBA8Unorm, VriFormat_RGBA8_UNORM},
                 {MTLPixelFormatBGRA8Unorm, VriFormat_BGRA8_UNORM}}};
    }
} // namespace vultra
