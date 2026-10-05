#pragma once
#include <vultra/core/math/extent.hpp>

#include <cstddef>
#include <filesystem>
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

    Image loadPng(const std::filesystem::path& path);
    Image loadPng(std::span<const std::byte> bytes);
    void  savePng(const Image& image, const std::filesystem::path& path);
    // Lossless float RGB output; PFM has no alpha channel, gamma or tone mapping.
    void savePfm(const Image& image, const std::filesystem::path& path);
    void dumpFrame(const Image& image, const std::filesystem::path& directory, uint64_t frame);
    // RGB PSNR; Rec.709-weighted luma SSIM, 11x11 Gaussian sigma=1.5, valid windows.
    // Alpha ignored. Both inputs must use the same color space and range. At least 11x11.
    ImageMetrics compare(const Image& reference, const Image& test, double peak = 1.0);
    void         validateImage(const Image& image);
} // namespace vultra
