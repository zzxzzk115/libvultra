#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/shader_asset.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/shader_material.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#if defined(VULTRA_WITH_SHADER_COMPILER) || defined(VULTRA_WITH_GAME_SHADER_COMPILER)
#error "This probe must not select a host shader compiler"
#endif

namespace
{
    constexpr unsigned char kProject[] = {
#include "project.vpk.h"
    };
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 2)
    {
        throw std::invalid_argument("Usage: platform-shader-material fresh-output-directory");
    }
    const std::filesystem::path output(argv[1]);
    if (!std::filesystem::create_directory(output))
    {
        throw std::invalid_argument("Use a fresh output directory");
    }
    const VpkArchive  pack("/embedded/project.vpk", std::as_bytes(std::span(kProject)));
    const AssetSource source(pack);
    Device            device;
    const bool        wgsl = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
    const auto asset = ShaderAsset::load(wgsl ? "material-wgsl.vshaderc" : "material-spirv_1_5.vshaderc", &source);
    constexpr Extent size {129, 73};
    Texture          target(device, colorTexture(size, VriFormat_RGBA8_UNORM));
    MaterialInstance instance;
    instance.shader = {StableId::generate()};
    ShaderMaterial    material(device, asset, instance);
    ShaderPassContext context;
    context.colors = {target.desc.format};
    {
        const auto wrongAsset =
            ShaderAsset::load(wgsl ? "material-spirv_1_5.vshaderc" : "material-wgsl.vshaderc", &source);
        MaterialInstance wrongInstance;
        ShaderMaterial   wrongMaterial(device, wrongAsset, wrongInstance);
        bool             rejected = false;
        try
        {
            wrongMaterial.prepare("Unlit", context);
        }
        catch (const std::invalid_argument& error)
        {
            rejected = std::string_view(error.what()).find("backend cannot consume") != std::string_view::npos;
        }
        if (!rejected)
        {
            throw std::runtime_error("Material target mismatch reached GPU pipeline creation");
        }
    }
    Frame        frame(device);
    float        maximumError  = 0;
    VriPipeline* firstPipeline = nullptr;
    for (uint32_t index = 0; index < 4; ++index)
    {
        if (index == 1)
        {
            instance.set(asset, "gain", 0.5f);
        }
        else if (index == 2)
        {
            instance.setVariant(asset, "Invert");
        }
        else if (index == 3)
        {
            // Persistence must restore the same typed overrides without retaining authoring source.
            instance.assign(asset, MaterialInstance::parse(instance.serialize()));
        }
        auto* pipeline = material.prepare("Unlit", context);
        if (index == 0)
        {
            firstPipeline = pipeline;
        }
        if ((index == 1 && pipeline != firstPipeline) || material.pipelineCount() != (index < 2 ? 1u : 2u))
        {
            throw std::runtime_error("Material value edits recreated a pipeline or a variant lost its identity");
        }
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
        // Authored Color is sRGB; this UNORM attachment stores the linear values sent to the shader.
        std::array<float, 4> expected {0.03310477f, 0.13286832f, 0.31854678f, 1};
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            if (index > 0)
            {
                expected[channel] *= 0.5f;
            }
            if (index > 1)
            {
                expected[channel] = 1 - expected[channel];
            }
        }
        for (size_t pixel = 0; pixel < size.width * size.height; ++pixel)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                maximumError = std::max(maximumError, std::abs(image.rgba[pixel * 4 + channel] - expected[channel]));
            }
        }
        savePng(image, output / ("frame-" + std::to_string(index) + ".png"));
    }
    if (maximumError > 1.5f / 255)
    {
        throw std::runtime_error("Cooked material disagrees with CPU reference; error " + std::to_string(maximumError));
    }
    std::cout << "SHADER_MATERIAL_PASSED: " << (wgsl ? "wgsl" : "spirv_1_5") << ", 4 frames, max error " << maximumError
              << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
