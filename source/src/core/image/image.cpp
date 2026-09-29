#include <vultra/core/image/image.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
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
        int   width;
        int   height;
        int   channels;
        auto* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
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

    ImageMetrics compare(const Image& reference, const Image& test, double peak)
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
        if (reference.size.width < 11 || reference.size.height < 11)
        {
            throw std::invalid_argument("SSIM requires at least 11x11 pixels");
        }
        ImageMetrics result;
        const size_t pixels = reference.rgba.size() / 4;
        for (size_t i = 0; i < pixels; ++i)
        {
            for (size_t c = 0; c < 3; ++c)
            {
                const double d = double(reference.rgba[i * 4 + c]) - test.rgba[i * 4 + c];
                result.mse += d * d;
            }
        }
        result.mse /= double(pixels) * 3;
        result.psnr     = result.mse == 0 ? std::numeric_limits<double>::infinity() :
                                            20 * std::log10(peak) - 10 * std::log10(result.mse);
        const auto   a  = luma(reference);
        const auto   b  = luma(test);
        const auto   ma = blur(a, reference.size);
        const auto   mb = blur(b, reference.size);
        const auto   aa = blur(multiply(a, a), reference.size);
        const auto   bb = blur(multiply(b, b), reference.size);
        const auto   ab = blur(multiply(a, b), reference.size);
        const double c1 = (0.01 * peak) * (0.01 * peak);
        const double c2 = (0.03 * peak) * (0.03 * peak);
        for (uint32_t y = 5; y < reference.size.height - 5; ++y)
        {
            for (uint32_t x = 5; x < reference.size.width - 5; ++x)
            {
                const size_t i          = size_t(y) * reference.size.width + x;
                const double va         = std::max(0.0, aa[i] - ma[i] * ma[i]);
                const double vb         = std::max(0.0, bb[i] - mb[i] * mb[i]);
                const double covariance = ab[i] - ma[i] * mb[i];
                result.ssim += ((2 * ma[i] * mb[i] + c1) * (2 * covariance + c2)) /
                               ((ma[i] * ma[i] + mb[i] * mb[i] + c1) * (va + vb + c2));
            }
        }
        result.ssim /= double(reference.size.width - 10) * (reference.size.height - 10);
        return result;
    }
} // namespace vultra
