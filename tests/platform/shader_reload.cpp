#include <vultra/assets/asset_source.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#if defined(VULTRA_WITH_SHADER_COMPILER) || defined(VULTRA_WITH_GAME_SHADER_COMPILER)
#error "Cooked material reload must not select a host compiler"
#endif

namespace
{
    constexpr unsigned char kProject[] = {
#include "project.vpk.h"
    };

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 2)
    {
        throw std::invalid_argument("Usage: platform-shader-reload fresh-output-directory");
    }
    const std::filesystem::path output(argv[1]);
    require(std::filesystem::create_directory(output), "Use a fresh output directory");
    const VpkArchive  pack("/embedded/project.vpk", std::as_bytes(std::span(kProject)));
    const AssetSource packedSource(pack);
    Device            device;
    const bool        wgsl       = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
    const auto        entry      = wgsl ? "material-wgsl.vshaderc" : "material-spirv_1_5.vshaderc";
    const auto        wrongEntry = wgsl ? "material-spirv_1_5.vshaderc" : "material-wgsl.vshaderc";
    const auto        original   = pack.read(entry);
    const auto        file       = output / "material.vshaderc";
    writeFileAtomically(file, original);
    constexpr Extent  size {129, 73};
    Texture           target(device, colorTexture(size, VriFormat_RGBA8_UNORM));
    ShaderPassContext context;
    context.colors = {target.desc.format};
    Frame    frame(device);
    uint32_t frames       = 0;
    float    maximumError = 0;
    auto     draw = [&](ShaderRuntime& runtime, uint32_t index, float gain, bool invert, const std::string& name)
    {
        auto& material = runtime.material(index);
        auto* pipeline = material.prepare("Unlit", context);
        auto* commands = frame.begin();
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        beginColorPass(device, commands, target.view(), size);
        material.bind(commands, pipeline);
        const VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(commands, &draw);
        device.core.CmdEndRendering(commands);
        frame.submitAndWait();
        const auto image = readback(device, target);
        // Non-HDR Color defaults are authored in sRGB and uploaded as these linear RGB values.
        const std::array<float, 4> color {0.03310477f, 0.13286832f, 0.31854678f, 1};
        for (size_t pixel = 0; pixel < size.width * size.height; ++pixel)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                float expected = color[channel];
                if (channel < 3)
                {
                    expected *= gain;
                    if (invert)
                    {
                        expected = 1 - expected;
                    }
                }
                maximumError = std::max(maximumError, std::abs(image.rgba[pixel * 4 + channel] - expected));
            }
        }
        savePng(image, output / (name + ".png"));
        ++frames;
        require(maximumError <= 1.5f / 255, "Cooked reload changed the expected material pixels");
    };
    {
        // The package path stays virtual; constructing from a relative entry must not materialize it.
        ShaderRuntime runtime(device, entry, {}, {}, {}, {}, &packedSource);
        const auto    index = runtime.addMaterial();
        draw(runtime, index, 1, false, "pack");
    }
    bool rejectedSource = false;
    try
    {
        ShaderRuntime source(device, output / "source.vshader");
    }
    catch (const std::invalid_argument& error)
    {
        rejectedSource = std::string_view(error.what()).find("requires a cooked") != std::string_view::npos;
    }
    require(rejectedSource, "Runtime-only source compilation was accepted");

    ShaderRuntime runtime(device, file);
    const auto    first  = runtime.addMaterial();
    const auto    second = runtime.addMaterial();
    runtime.instance(first).set(runtime.asset(), "gain", 0.5f);
    runtime.instance(second).set(runtime.asset(), "gain", 0.25f);
    runtime.instance(second).setVariant(runtime.asset(), "Invert");
    auto drawPair = [&](const std::string& phase)
    {
        draw(runtime, first, 0.5f, false, phase + "-0");
        draw(runtime, second, 0.25f, true, phase + "-1");
    };
    drawPair("before");
    const auto  generation     = runtime.generation();
    const auto* previousFirst  = &runtime.material(first);
    const auto* previousSecond = &runtime.material(second);
    auto        retained       = [&]
    {
        require(runtime.generation() == generation && &runtime.material(first) == previousFirst &&
                    &runtime.material(second) == previousSecond,
                "Failed reload partially published a material snapshot");
    };
    auto prepare = [&](uint32_t, ShaderMaterial& material)
    {
        material.prepare("Unlit", context);
    };
    require(!runtime.reload(), "Prepared materials reloaded without a preparation callback");
    retained();
    const std::array<std::byte, 4> invalid {};
    writeFileAtomically(file, invalid);
    require(!runtime.reload(prepare), "Corrupt cooked data replaced the live snapshot");
    retained();
    drawPair("bad-file");
    writeFileAtomically(file, original);
    uint32_t prepared = 0;
    require(!runtime.reload(
                [&](uint32_t index, ShaderMaterial& material)
                {
                    prepare(index, material);
                    ++prepared;
                    if (index == second)
                    {
                        throw std::runtime_error("Reject the second prepared material");
                    }
                }) &&
                prepared == 2,
            "GPU preparation failure was not transactional across both materials");
    retained();
    drawPair("bad-prepare");
    writeFileAtomically(file, pack.read(wrongEntry));
    require(!runtime.reload(prepare), "A different backend's program replaced the live snapshot");
    retained();
    drawPair("bad-target");
    writeFileAtomically(file, original);
    require(runtime.reload(prepare) && runtime.generation() == generation + 1,
            "Cooked material reload did not recover after failure");
    require(&runtime.material(first) != previousFirst && &runtime.material(second) != previousSecond,
            "Successful publication retained old GPU material references");
    drawPair("recovered");
    std::cout << "SHADER_RELOAD_PASSED: " << (wgsl ? "wgsl" : "spirv_1_5") << ", " << frames << " frames, max error "
              << maximumError << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
