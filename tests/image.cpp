#include <vultra/core/image/image.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void require(bool ok, const char* message)
    {
        if (!ok)
        {
            throw std::runtime_error(message);
        }
    }

    template<class F>
    void reject(F&& f)
    {
        bool rejected = false;
        try
        {
            f();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Expected invalid input rejection");
    }

    // Independent direct 2D SSIM reference (no separable convolution).
    double directSsim(const vultra::Image& a, const vultra::Image& b)
    {
        auto luma = [](const auto& image, int x, int y)
        {
            const auto i = (size_t(y) * image.size.width + x) * 4;
            return 0.2126 * image.rgba[i] + 0.7152 * image.rgba[i + 1] + 0.0722 * image.rgba[i + 2];
        };
        double total = 0;
        int    count = 0;
        for (int y = 5; y < int(a.size.height) - 5; ++y)
        {
            for (int x = 5; x < int(a.size.width) - 5; ++x)
            {
                double weight = 0;
                double ma     = 0;
                double mb     = 0;
                double aa     = 0;
                double bb     = 0;
                double ab     = 0;
                for (int j = -5; j <= 5; ++j)
                {
                    for (int i = -5; i <= 5; ++i)
                    {
                        const double w  = std::exp(-double(i * i + j * j) / 4.5);
                        const double av = luma(a, x + i, y + j);
                        const double bv = luma(b, x + i, y + j);
                        weight += w;
                        ma += w * av;
                        mb += w * bv;
                        aa += w * av * av;
                        bb += w * bv * bv;
                        ab += w * av * bv;
                    }
                }
                ma /= weight;
                mb /= weight;
                aa /= weight;
                bb /= weight;
                ab /= weight;
                total += ((2 * ma * mb + 0.0001) * (2 * (ab - ma * mb) + 0.0009)) /
                         ((ma * ma + mb * mb + 0.0001) * (aa - ma * ma + bb - mb * mb + 0.0009));
                ++count;
            }
        }
        return total / count;
    }
} // namespace
int main()
try
{
    using namespace vultra;
    Image a {{19, 17}, std::vector<float>(19 * 17 * 4, 0)};
    Image b     = a;
    auto  equal = compare(a, b);
    require(equal.mse == 0 && std::isinf(equal.psnr) && std::abs(equal.ssim - 1) < 1e-12, "Identity metrics");
    std::fill(b.rgba.begin(), b.rgba.end(), 1.0f);
    auto contrast = compare(a, b);
    require(std::abs(contrast.psnr) < 1e-12 && std::abs(contrast.ssim - 0.0001 / 1.0001) < 1e-10,
            "Constant contrast metrics");
    for (size_t i = 0; i < a.rgba.size(); ++i)
    {
        a.rgba[i] = float((i * 17) % 101) / 100;
        b.rgba[i] = a.rgba[i] * 0.7f + 0.1f;
    }
    require(std::abs(compare(a, b).ssim - directSsim(a, b)) < 1e-10, "SSIM differs from direct 2D reference");
    b = a;
    for (size_t i = 3; i < b.rgba.size(); i += 4)
    {
        b.rgba[i] = 1 - b.rgba[i];
    }
    require(compare(a, b).mse == 0, "Alpha should not affect RGB metrics");
    reject(
        [&]
        {
            compare(a, b, 0);
        });
    reject(
        [&]
        {
            compare(a, b, std::numeric_limits<double>::infinity());
        });
    b.rgba[0] = std::numeric_limits<float>::quiet_NaN();
    reject(
        [&]
        {
            compare(a, b);
        });
    b = a;
    b.rgba.pop_back();
    reject(
        [&]
        {
            compare(a, b);
        });
    b = {{10, 10}, std::vector<float>(400)};
    reject(
        [&]
        {
            compare(b, b);
        });
    const auto path = std::filesystem::path("build/.tmp/image-tests/roundtrip.png");
    savePng(a, path);
    const auto loaded = loadPng(path);
    require(loaded.size == a.size, "PNG dimensions changed");
    for (size_t i = 0; i < a.rgba.size(); ++i)
    {
        require(std::abs(loaded.rgba[i] - a.rgba[i]) <= 0.5f / 255 + 1e-7, "PNG quantization/alpha");
    }
    std::cout << "Image tests passed: analytic metrics, direct SSIM reference, invalid inputs, RGBA PNG roundtrip\n";
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
