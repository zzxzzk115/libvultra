#pragma once
#include <vultra/core/base/error.hpp>
#include <vultra/core/math/extent.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace vultra
{
    // Top-left origin, tightly packed RGBA floats. No implicit gamma/tone mapping.
    struct Image
    {
        Extent             size;
        std::vector<float> rgba;
    };

    struct ImageMetrics
    {
        double mse  = 0;
        double psnr = 0;
        double ssim = 0;
    };

    struct MetricRegion
    {
        uint32_t x      = 0;
        uint32_t y      = 0;
        uint32_t width  = 0; // Both zero select the full frame.
        uint32_t height = 0;
    };

    struct RegionMetrics
    {
        uint64_t              pixels      = 0;
        uint64_t              ssimWindows = 0;
        std::optional<double> mse;
        std::optional<double> psnr;
        std::optional<double> ssim;
    };

    // Binary mask: one finite 0 or 1 per image pixel. SSIM includes selected window centers;
    // each complete 11x11 window must fit inside the ROI. Empty selections have absent metrics.
    [[nodiscard]] std::expected<MetricRegion, Error>
    validateMetricRegion(Extent size, MetricRegion region, std::span<const float> mask = {});
    [[nodiscard]] std::expected<RegionMetrics, Error> compareRegion(const Image&           reference,
                                                                    const Image&           test,
                                                                    MetricRegion           region = {},
                                                                    std::span<const float> mask   = {},
                                                                    double                 peak   = 1);

    enum class ImageChannel : uint32_t
    {
        eRgb,
        eRed,
        eGreen,
        eBlue,
        eAlpha,
        eLuminance
    };

    // Explicit display mapping. Original capture data is never modified or implicitly gamma converted.
    struct ImageView
    {
        ImageChannel channel = ImageChannel::eRgb;
        float        minimum = 0;
        float        maximum = 1;
    };

    [[nodiscard]] std::expected<void, Error>                 validateImageView(const ImageView& view);
    [[nodiscard]] std::expected<Image, Error>                mapImage(const Image& image, const ImageView& view);
    [[nodiscard]] std::expected<std::array<float, 4>, Error> imagePixel(const Image& image, uint32_t x, uint32_t y);

    [[nodiscard]] std::expected<Image, Error> loadPng(const std::filesystem::path& path);
    [[nodiscard]] std::expected<Image, Error> loadPng(std::span<const std::byte> bytes);
    [[nodiscard]] std::expected<void, Error>  savePng(const Image& image, const std::filesystem::path& path);
    // Lossless float RGB output; PFM has no alpha channel, gamma or tone mapping.
    [[nodiscard]] std::expected<void, Error> savePfm(const Image& image, const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, Error>
    dumpFrame(const Image& image, const std::filesystem::path& directory, uint64_t frame);
    // RGB PSNR; Rec.709-weighted luma SSIM, 11x11 Gaussian sigma=1.5, valid windows.
    // Alpha ignored. Both inputs must use the same color space and range. At least 11x11.
    [[nodiscard]] std::expected<ImageMetrics, Error>
                                             compare(const Image& reference, const Image& test, double peak = 1.0);
    [[nodiscard]] std::expected<void, Error> validateImage(const Image& image);
} // namespace vultra
