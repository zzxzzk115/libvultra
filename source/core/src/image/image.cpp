#include <vultra/core/image/image.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    void validateImage(const Image& image)
    {
        if (image.size.empty() || image.size.width > uint32_t(INT_MAX / 4) || image.size.height > uint32_t(INT_MAX) ||
            uint64_t(image.size.width) * image.size.height * 4 != image.rgba.size())
        {
            throw std::invalid_argument("Invalid image dimensions or RGBA storage");
        }
        if (!std::all_of(image.rgba.begin(),
                         image.rgba.end(),
                         [](float v)
                         {
                             return std::isfinite(v);
                         }))
        {
            throw std::invalid_argument("Image contains NaN or infinity");
        }
    }

    void validateImageView(const ImageView& view)
    {
        if (view.channel > ImageChannel::eLuminance || !std::isfinite(view.minimum) || !std::isfinite(view.maximum) ||
            view.maximum <= view.minimum || !std::isfinite(view.maximum - view.minimum))
        {
            throw std::invalid_argument("Image view requires a valid channel and finite increasing range");
        }
    }

    std::array<float, 4> imagePixel(const Image& image, uint32_t x, uint32_t y)
    {
        if (x >= image.size.width || y >= image.size.height ||
            image.rgba.size() != size_t(image.size.width) * image.size.height * 4)
        {
            throw std::invalid_argument("Pixel probe is outside the image");
        }
        const auto offset = (size_t(y) * image.size.width + x) * 4;
        return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
    }

    Image mapImage(const Image& image, const ImageView& view)
    {
        validateImage(image);
        validateImageView(view);
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
