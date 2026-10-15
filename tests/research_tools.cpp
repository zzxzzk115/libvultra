#include <vultra/drivers/rhi/device.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/research/camera_track.hpp>
#include <vultra/research/quality.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    using namespace vultra;

    void require(bool ok, const char* message)
    {
        if (!ok)
        {
            throw std::runtime_error(message);
        }
    }

    template<class F>
    void reject(F&& action)
    {
        bool failed = false;
        try
        {
            action();
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, "Invalid research input was accepted");
    }

    Image constant(float value)
    {
        Image result {{24, 24}, std::vector<float>(24 * 24 * 4, value)};
        for (size_t i = 3; i < result.rgba.size(); i += 4)
        {
            result.rgba[i] = 1;
        }
        return result;
    }

    void imageQuality()
    {
        const auto a   = constant(0);
        const auto b   = constant(0.25f);
        const auto hdr = compare(a, constant(2));
        require(std::abs(hdr.mse - 4) < 1e-8 && std::abs(hdr.psnr + 10 * std::log10(4.0)) < 1e-8,
                "Unbounded HDR can have negative PSNR at peak 1; do not clamp or normalize its errors");
        std::vector<float> mask(24 * 24, 0);
        mask[12 * 24 + 12]  = 1;
        const auto selected = compareRegion(a, b, {}, mask);
        require(selected.pixels == 1 && selected.ssimWindows == 1 && std::abs(*selected.mse - .0625) < 1e-8 &&
                    std::abs(*selected.ssim - .0001 / (.0625 + .0001)) < 1e-8,
                "Masked analytic metrics");
        const auto roi = compareRegion(a, b, {2, 3, 12, 13});
        require(roi.pixels == 156 && roi.ssimWindows == 6, "ROI excludes SSIM windows outside its boundary");
        std::ranges::fill(mask, 0.0f);
        const auto empty = compareRegion(a, b, {}, mask);
        require(!empty.mse && !empty.psnr && !empty.ssim && empty.pixels == 0,
                "Empty mask must not report a perfect result");
        require(!compareRegion(a, b, {0, 0, 2, 2}).ssim, "Tiny ROI has no SSIM windows");
        mask[0] = std::numeric_limits<float>::quiet_NaN();
        reject(
            [&]
            {
                compareRegion(a, b, {}, mask);
            });
        reject(
            [&]
            {
                compareRegion(a, b, {23, 0, 2, 11});
            });
        const auto perfect = evaluateFlip(b, b);
        require(perfect.mean && *perfect.mean < 1e-7, "FLIP identical images");
        const auto different = evaluateFlip(a, b);
        require(different.mean && *different.mean > .01 && *different.mean <= 1 && different.pixels == 24 * 24,
                "FLIP visible luminance difference");
        validateImage(different.error);
        const auto excluded = evaluateFlip(a, b, 67, {}, std::vector<float>(24 * 24, 0));
        require(!excluded.mean && excluded.pixels == 0, "FLIP empty mask reduction");
        reject(
            [&]
            {
                evaluateFlip(a, constant(4));
            });
        reject(
            [&]
            {
                evaluateFlip(a, b, 0);
            });
        require(std::abs(*temporalError(a, a, b, b)) < 1e-9, "Reference motion is removed from temporal error");
        require(std::abs(*temporalError(a, a, b, constant(.35f)) - .1) < 1e-7, "Temporal residual MAE");
    }

    void persistence(const std::filesystem::path& root)
    {
        CameraPose first;
        first.position   = {1, 2, 3};
        CameraPose last  = first;
        last.position.x  = 11;
        last.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        CameraTrack track {{{0, first}, {10, last}}};
        const auto  middle = track.evaluate(5);
        require(std::abs(middle.position.x - 6) < 1e-6 && glm::length(middle.orientation * glm::vec3(0, 0, -1) -
                                                                      glm::vec3(-.70710678f, 0, -.70710678f)) < 1e-5,
                "Track position/SLERP interpolation");
        require(track.evaluate(10).position == last.position && track.evaluate(0).position == first.position &&
                    track.evaluate(5).position == middle.position,
                "Frame evaluation is independent of call order");
        track.save(root / "track.json");
        require(CameraTrack::load(root / "track.json").serialize() == track.serialize(), "Track round trip");
        reject(
            [&]
            {
                CameraTrack {{{2, first}, {2, last}}}.validate();
            });
        auto broken                      = nlohmann::json::parse(track.serialize());
        broken["keys"][0]["orientation"] = {0, 0, 0, 0};
        reject(
            [&]
            {
                CameraTrack::parse(broken.dump());
            });
    }

    void stagedReadback()
    {
        Device device;
        for (const auto format : {VriFormat_RGBA32_SFLOAT, VriFormat_RGBA16_SFLOAT, VriFormat_BGRA8_UNORM})
        {
            Texture texture(device, colorTexture({23, 17}, format));
            Frame   frame(device);
            auto*   cmd = frame.begin();
            texture.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float firstColor[] {.25f, .5f, .75f, 1};
            beginColorPass(device, cmd, texture.view(), {23, 17}, firstColor);
            device.core.CmdEndRendering(cmd);
            ImageReadback first(device, texture);
            reject(
                [&]
                {
                    first.consume();
                });
            first.record(cmd, texture);
            reject(
                [&]
                {
                    first.record(cmd, texture);
                });
            const float secondColor[] {.75f, .25f, .5f, 1};
            beginColorPass(device, cmd, texture.view(), {23, 17}, secondColor);
            device.core.CmdEndRendering(cmd);
            ImageReadback second(device, texture);
            second.record(cmd, texture);
            frame.submitAndWait();
            const auto a = first.consume();
            const auto b = second.consume();
            require(a.size == Extent {23, 17} && b.size == a.size, "Staged readback extent");
            for (size_t pixel = 0; pixel < a.rgba.size() / 4; ++pixel)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    require(std::abs(a.rgba[pixel * 4 + channel] - firstColor[channel]) < .005f &&
                                std::abs(b.rgba[pixel * 4 + channel] - secondColor[channel]) < .005f,
                            "Staged copies must preserve the sampled frame before later writes");
                }
            }
        }
    }
} // namespace

int main()
try
{
    const auto root = std::filesystem::path("build/.tmp/research-tools") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    imageQuality();
    persistence(root);
    stagedReadback();
    std::cout << "Research tools passed: deterministic tracks, ROI/masks, FLIP, temporal error "
                 "and staged readback\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
