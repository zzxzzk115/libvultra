#include <vultra/core/image/image.hpp>

#include <algorithm>
#include <climits>
#include <cmath>

namespace vultra
{
    std::expected<void, Error> validateImage(const Image& image)
    {
        if (image.size.empty() || image.size.width > uint32_t(INT_MAX / 4) || image.size.height > uint32_t(INT_MAX) ||
            uint64_t(image.size.width) * image.size.height * 4 != image.rgba.size())
        {
            return makeError("Invalid image dimensions or RGBA storage");
        }
        if (!std::all_of(image.rgba.begin(),
                         image.rgba.end(),
                         [](float v)
                         {
                             return std::isfinite(v);
                         }))
        {
            return makeError("Image contains NaN or infinity");
        }
        return {};
    }

    std::expected<void, Error> validateImageView(const ImageView& view)
    {
        if (view.channel > ImageChannel::eLuminance || !std::isfinite(view.minimum) || !std::isfinite(view.maximum) ||
            view.maximum <= view.minimum || !std::isfinite(view.maximum - view.minimum))
        {
            return makeError("Image view requires a valid channel and finite increasing range");
        }
        return {};
    }

    std::expected<std::array<float, 4>, Error> imagePixel(const Image& image, uint32_t x, uint32_t y)
    {
        if (x >= image.size.width || y >= image.size.height ||
            image.rgba.size() != size_t(image.size.width) * image.size.height * 4)
        {
            return makeError("Pixel probe is outside the image");
        }
        const auto offset = (size_t(y) * image.size.width + x) * 4;
        return std::array {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
    }

    std::expected<Image, Error> mapImage(const Image& image, const ImageView& view)
    {
        if (auto result = validateImage(image); !result)
        {
            return std::unexpected(result.error());
        }
        if (auto result = validateImageView(view); !result)
        {
            return std::unexpected(result.error());
        }
        Image result {image.size, std::vector<float>(image.rgba.size())};
        for (size_t i = 0; i < image.rgba.size(); i += 4)
        {
            auto rgb = std::array {image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]};
            if (view.channel != ImageChannel::eRgb)
            {
                const auto scalar = view.channel == ImageChannel::eLuminance ?
                                        rgb[0] * 0.2126f + rgb[1] * 0.7152f + rgb[2] * 0.0722f :
                                        image.rgba[i + uint32_t(view.channel) - 1];
                rgb.fill(scalar);
            }
            for (size_t channel = 0; channel < 3; ++channel)
            {
                result.rgba[i + channel] =
                    std::clamp((rgb[channel] - view.minimum) / (view.maximum - view.minimum), 0.0f, 1.0f);
            }
            result.rgba[i + 3] = 1;
        }
        return result;
    }
} // namespace vultra
