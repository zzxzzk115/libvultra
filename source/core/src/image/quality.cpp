#include <vultra/core/image/quality.hpp>

#include <flip/FLIP.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    FlipResult evaluateFlip(const Image&           reference,
                            const Image&           test,
                            float                  pixelsPerDegree,
                            MetricRegion           region,
                            std::span<const float> mask)
    {
        validateImage(reference);
        validateImage(test);
        if (reference.size != test.size || !std::isfinite(pixelsPerDegree) || pixelsPerDegree < 1 ||
            pixelsPerDegree > 1000)
        {
            throw std::invalid_argument("FLIP needs matching extents and pixels-per-degree in [1,1000]");
        }
        region = validateMetricRegion(reference.size, region, mask);
        FLIP::image<FLIP::color3> a(int(reference.size.width), int(reference.size.height));
        FLIP::image<FLIP::color3> b(int(reference.size.width), int(reference.size.height));
        for (uint32_t y = 0; y < reference.size.height; ++y)
        {
            for (uint32_t x = 0; x < reference.size.width; ++x)
            {
                const auto r = imagePixel(reference, x, y);
                const auto t = imagePixel(test, x, y);
                for (size_t c = 0; c < 3; ++c)
                {
                    if (r[c] < 0 || r[c] > 1 || t[c] < 0 || t[c] > 1)
                    {
                        throw std::invalid_argument("LDR-FLIP requires explicitly tone-mapped linear RGB in [0,1]");
                    }
                }
                a.set(int(x), int(y), FLIP::color3(r[0], r[1], r[2]));
                b.set(int(x), int(y), FLIP::color3(t[0], t[1], t[2]));
            }
        }
        FLIP::Parameters parameters;
        parameters.PPD = pixelsPerDegree;
        FLIP::image<float> errors(int(reference.size.width), int(reference.size.height), 0.0f);
        FLIP::evaluate(a, b, false, parameters, errors);
        FlipResult result {{reference.size, std::vector<float>(reference.rgba.size())}, {}, 0, pixelsPerDegree};
        double     sum = 0;
        for (uint32_t y = 0; y < reference.size.height; ++y)
        {
            for (uint32_t x = 0; x < reference.size.width; ++x)
            {
                const size_t i     = size_t(y) * reference.size.width + x;
                const float  error = errors.get(int(x), int(y));
                std::fill_n(result.error.rgba.data() + i * 4, 3, error);
                result.error.rgba[i * 4 + 3] = 1;
                if (x >= region.x && x - region.x < region.width && y >= region.y && y - region.y < region.height &&
                    (mask.empty() || mask[i] == 1))
                {
                    sum += error;
                    ++result.pixels;
                }
            }
        }
        if (result.pixels)
        {
            result.mean = sum / result.pixels;
        }
        return result;
    }

    std::optional<double> temporalError(const Image&           referenceBefore,
                                        const Image&           testBefore,
                                        const Image&           referenceNow,
                                        const Image&           testNow,
                                        MetricRegion           region,
                                        std::span<const float> mask)
    {
        for (const auto* image : {&referenceBefore, &testBefore, &referenceNow, &testNow})
        {
            validateImage(*image);
            if (image->size != referenceNow.size)
            {
                throw std::invalid_argument("Temporal error images must have matching extents");
            }
        }
        region         = validateMetricRegion(referenceNow.size, region, mask);
        uint64_t count = 0;
        double   sum   = 0;
        for (uint32_t y = region.y; y < region.y + region.height; ++y)
        {
            for (uint32_t x = region.x; x < region.x + region.width; ++x)
            {
                const size_t i = size_t(y) * referenceNow.size.width + x;
                if (!mask.empty() && mask[i] == 0)
                {
                    continue;
                }
                for (size_t c = 0; c < 3; ++c)
                {
                    const auto k = i * 4 + c;
                    sum += std::abs(double(testNow.rgba[k]) - testBefore.rgba[k] -
                                    (double(referenceNow.rgba[k]) - referenceBefore.rgba[k]));
                }
                ++count;
            }
        }
        return count ? std::optional(sum / (count * 3)) : std::nullopt;
    }
} // namespace vultra
