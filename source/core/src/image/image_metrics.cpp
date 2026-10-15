#include <vultra/core/image/image.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vultra
{
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
