#include "../../examples/common/colored_mesh.hpp"
#include "../support/check.hpp"

#include <vultra/assets/vpk_archive.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    constexpr unsigned char kProject[] = {
#include "project.vpk.h"
    };
} // namespace

int main(int argc, char** argv)
{
    using namespace vultra;
    using support::must;
    if (argc != 2)
    {
        support::fail("Usage: platform-shader-runtime output.png");
    }
    const std::filesystem::path output(argv[1]);
    if (std::filesystem::exists(output))
    {
        support::fail("Use a fresh output image path");
    }
    const auto       pack      = must(VpkArchive::create("/embedded/project.vpk", std::as_bytes(std::span(kProject))));
    const auto       program   = must(ShaderProgram::load(must(pack.read("mesh.vshaderc")), "mesh.vshaderc"));
    const auto       devicePtr = must(Device::create());
    auto&            device    = *devicePtr;
    constexpr Extent size {129, 73};
    auto             target       = must(Texture::create(device, colorTexture(size, VriFormat_RGBA8_UNORM)));
    const auto       frame        = must(Frame::create(device));
    const auto       triangleMesh = must(sample::ColoredMesh::create(device,
                                                                     target.desc.format,
                                                                     sample::kTriangleVertices,
                                                                     sample::kTriangleIndices,
                                                                     VriPrimitiveTopology_TriangleList,
                                                                     false,
                                                                     &program));
    auto&            triangle     = *triangleMesh;
    // A translated, nonuniform scale catches transposed matrices and wrong push-constant packing.
    triangle.parameters.transform[0]  = 0.7f;
    triangle.parameters.transform[5]  = 0.8f;
    triangle.parameters.transform[12] = 0.1f;
    triangle.parameters.transform[13] = -0.05f;
    for (uint32_t index = 0; index < 3; ++index)
    {
        triangle.parameters.tint[0] = float(index + 1) / 12;
        triangle.parameters.tint[1] = float(index + 1) / 6;
        triangle.parameters.tint[2] = float(index + 1) / 4;
        auto* commands              = must(frame->begin());
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        must(triangle.draw(commands, target, clear));
        must(frame->submitAndWait());
    }
    const auto image        = must(readback(device, target));
    size_t     checked      = 0;
    float      maximumError = 0;
    for (uint32_t y = 0; y < size.height; ++y)
    {
        for (uint32_t x = 0; x < size.width; ++x)
        {
            const float  localX = ((2 * (float(x) + 0.5f) / size.width - 1) - 0.1f) / 0.7f;
            const float  localY = ((1 - 2 * (float(y) + 0.5f) / size.height) + 0.05f) / 0.8f;
            const float  red    = localY + 0.5f;
            const float  blue   = localX + 0.5f * (1 - red);
            const float  green  = 1 - red - blue;
            const size_t offset = (y * size.width + x) * 4;
            // Skip rasterization edges; compare interpolated linear RGB against CPU barycentric weights.
            if (std::min({red, green, blue}) > 0.02f)
            {
                const float expected[3] {red * 0.25f, green * 0.5f, blue * 0.75f};
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    maximumError = std::max(maximumError, std::abs(image.rgba[offset + channel] - expected[channel]));
                }
                ++checked;
            }
            if (image.rgba[offset + 3] != 1)
            {
                support::fail("Cooked shader changed target alpha");
            }
        }
    }
    must(savePng(image, output));
    if (checked < 100 || maximumError > 1.5f / 255)
    {
        support::fail("Cooked shader transform/tint disagrees with CPU reference; error " +
                      std::to_string(maximumError));
    }
    std::cout << "SHADER_RUNTIME_PASSED: " << program.profile << ", " << checked << " pixels, max error "
              << maximumError << ", adapter " << device.core.GetDeviceDesc(device.handle)->adapter.name << '\n';
    return 0;
}
