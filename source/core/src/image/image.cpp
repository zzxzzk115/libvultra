#include <vultra/core/image/image.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
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

    Image loadPng(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Cannot open PNG: " + path.string());
        }
        const auto length = input.tellg();
        if (length <= 0 || length > INT_MAX)
        {
            throw std::runtime_error("PNG file size is unsupported");
        }
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input)
        {
            throw std::runtime_error("Cannot read PNG");
        }
        return loadPng(std::as_bytes(std::span(bytes)));
    }

    Image loadPng(std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > INT_MAX)
        {
            throw std::invalid_argument("PNG data size is unsupported");
        }
        int   width;
        int   height;
        int   channels;
        auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                             int(bytes.size()),
                                             &width,
                                             &height,
                                             &channels,
                                             4);
        if (!pixels)
        {
            throw std::runtime_error(std::string("PNG decode failed: ") + stbi_failure_reason());
        }
        Image image {{uint32_t(width), uint32_t(height)}, {}};
        try
        {
            image.rgba.resize(size_t(width) * height * 4);
            std::transform(pixels,
                           pixels + image.rgba.size(),
                           image.rgba.begin(),
                           [](unsigned char v)
                           {
                               return float(v) / 255;
                           });
        }
        catch (...)
        {
            stbi_image_free(pixels);
            throw;
        }
        stbi_image_free(pixels);
        return image;
    }

    void savePng(const Image& image, const std::filesystem::path& path)
    {
        validateImage(image);
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::vector<unsigned char> bytes(image.rgba.size());
        std::transform(image.rgba.begin(),
                       image.rgba.end(),
                       bytes.begin(),
                       [](float v)
                       {
                           return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f);
                       });
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Cannot create PNG: " + path.string());
        }
        const auto write = [](void* context, void* data, int size)
        {
            static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
        };
        const auto success = stbi_write_png_to_func(write,
                                                    &output,
                                                    int(image.size.width),
                                                    int(image.size.height),
                                                    4,
                                                    bytes.data(),
                                                    int(image.size.width * 4));
        output.flush();
        if (!success || !output)
        {
            throw std::runtime_error("PNG write failed: " + path.string());
        }
    }

    void savePfm(const Image& image, const std::filesystem::path& path)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        validateImage(image);
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Cannot create PFM: " + path.string());
        }
        output << "PF\n"
               << image.size.width << ' ' << image.size.height << '\n'
               << (std::endian::native == std::endian::little ? "-1.0\n" : "1.0\n");
        std::vector<float> row(size_t(image.size.width) * 3);
        // PFM stores RGB from bottom to top; Image stores RGBA from top to bottom.
        for (uint32_t y = image.size.height; y > 0; --y)
        {
            for (uint32_t x = 0; x < image.size.width; ++x)
            {
                const auto source = (size_t(y - 1) * image.size.width + x) * 4;
                std::copy_n(image.rgba.data() + source, 3, row.data() + size_t(x) * 3);
            }
            output.write(reinterpret_cast<const char*>(row.data()), std::streamsize(row.size() * sizeof(float)));
        }
        output.flush();
        if (!output)
        {
            throw std::runtime_error("PFM write failed: " + path.string());
        }
    }

    void dumpFrame(const Image& image, const std::filesystem::path& directory, uint64_t frame)
    {
        std::ostringstream name;
        name << "frame_" << std::setfill('0') << std::setw(6) << frame << ".png";
        const auto path = directory / name.str();
        if (std::filesystem::exists(path))
        {
            throw std::runtime_error("Frame already exists: " + path.string());
        }
        savePng(image, path);
    }

    namespace
    {
        using Plane = std::vector<double>;

        Plane luma(const Image& image)
        {
            Plane result(size_t(image.size.width) * image.size.height);
            for (size_t i = 0; i < result.size(); ++i)
            {
                result[i] =
                    0.2126 * image.rgba[4 * i] + 0.7152 * image.rgba[4 * i + 1] + 0.0722 * image.rgba[4 * i + 2];
            }
            return result;
        }

        Plane multiply(const Plane& a, const Plane& b)
        {
            Plane result(a.size());
            for (size_t i = 0; i < result.size(); ++i)
            {
                result[i] = a[i] * b[i];
            }
            return result;
        }

        Plane blur(const Plane& input, Extent size)
        {
            double kernel[11];
            double sum = 0;
            for (int i = -5; i <= 5; ++i)
            {
                kernel[i + 5] = std::exp(-double(i * i) / (2 * 1.5 * 1.5));
                sum += kernel[i + 5];
            }
            for (auto& k : kernel)
            {
                k /= sum;
            }
            Plane     temporary(input.size());
            Plane     output(input.size());
            const int w = int(size.width);
            const int h = int(size.height);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    for (int k = -5; k <= 5; ++k)
                    {
                        temporary[size_t(y) * w + x] +=
                            kernel[k + 5] * input[size_t(y) * w + std::clamp(x + k, 0, w - 1)];
                    }
                }
            }
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    for (int k = -5; k <= 5; ++k)
                    {
                        output[size_t(y) * w + x] +=
                            kernel[k + 5] * temporary[size_t(std::clamp(y + k, 0, h - 1)) * w + x];
                    }
                }
            }
            return output;
        }
    } // namespace

    MetricRegion validateMetricRegion(Extent size, MetricRegion region, std::span<const float> mask)
    {
        if (!region.width && !region.height)
        {
            region = {0, 0, size.width, size.height};
        }
        if (!region.width || !region.height || region.x >= size.width || region.y >= size.height ||
            region.width > size.width - region.x || region.height > size.height - region.y)
        {
            throw std::invalid_argument("Metric ROI is empty or outside the image");
        }
        if (!mask.empty() && (mask.size() != size_t(size.width) * size.height ||
                              !std::ranges::all_of(mask,
                                                   [](float value)
                                                   {
                                                       return value == 0 || value == 1;
                                                   })))
        {
            throw std::invalid_argument("Metric mask requires one binary 0/1 value per image pixel");
        }
        return region;
    }

    RegionMetrics compareRegion(const Image&           reference,
                                const Image&           test,
                                MetricRegion           region,
                                std::span<const float> mask,
                                double                 peak)
    {
        validateImage(reference);
        validateImage(test);
        if (reference.size != test.size)
        {
            throw std::invalid_argument("Image sizes differ");
        }
        if (!(peak > 0) || !std::isfinite(peak))
        {
            throw std::invalid_argument("Peak must be positive and finite");
        }
        region = validateMetricRegion(reference.size, region, mask);
        RegionMetrics result;
        double        mse    = 0;
        const size_t  pixels = reference.rgba.size() / 4;
        for (size_t i = 0; i < pixels; ++i)
        {
            const auto x = uint32_t(i % reference.size.width);
            const auto y = uint32_t(i / reference.size.width);
            if (x < region.x || x - region.x >= region.width || y < region.y || y - region.y >= region.height ||
                (!mask.empty() && mask[i] == 0))
            {
                continue;
            }
            ++result.pixels;
            for (size_t c = 0; c < 3; ++c)
            {
                const double d = double(reference.rgba[i * 4 + c]) - test.rgba[i * 4 + c];
                mse += d * d;
            }
        }
        if (result.pixels)
        {
            result.mse  = mse / (double(result.pixels) * 3);
            result.psnr = *result.mse == 0 ? std::numeric_limits<double>::infinity() :
                                             20 * std::log10(peak) - 10 * std::log10(*result.mse);
        }
        if (region.width < 11 || region.height < 11 || !result.pixels)
        {
            return result;
        }
        const auto   a    = luma(reference);
        const auto   b    = luma(test);
        const auto   ma   = blur(a, reference.size);
        const auto   mb   = blur(b, reference.size);
        const auto   aa   = blur(multiply(a, a), reference.size);
        const auto   bb   = blur(multiply(b, b), reference.size);
        const auto   ab   = blur(multiply(a, b), reference.size);
        const double c1   = (0.01 * peak) * (0.01 * peak);
        const double c2   = (0.03 * peak) * (0.03 * peak);
        double       ssim = 0;
        for (uint32_t y = region.y + 5; y < region.y + region.height - 5; ++y)
        {
            for (uint32_t x = region.x + 5; x < region.x + region.width - 5; ++x)
            {
                const size_t i = size_t(y) * reference.size.width + x;
                if (!mask.empty() && mask[i] == 0)
                {
                    continue;
                }
                ++result.ssimWindows;
                const double va         = std::max(0.0, aa[i] - ma[i] * ma[i]);
                const double vb         = std::max(0.0, bb[i] - mb[i] * mb[i]);
                const double covariance = ab[i] - ma[i] * mb[i];
                ssim += ((2 * ma[i] * mb[i] + c1) * (2 * covariance + c2)) /
                        ((ma[i] * ma[i] + mb[i] * mb[i] + c1) * (va + vb + c2));
            }
        }
        if (result.ssimWindows)
        {
            result.ssim = ssim / double(result.ssimWindows);
        }
        return result;
    }

    ImageMetrics compare(const Image& reference, const Image& test, double peak)
    {
        const auto metrics = compareRegion(reference, test, {}, {}, peak);
        if (!metrics.ssim)
        {
            throw std::invalid_argument("SSIM requires at least 11x11 pixels");
        }
        return {*metrics.mse, *metrics.psnr, *metrics.ssim};
    }
} // namespace vultra
