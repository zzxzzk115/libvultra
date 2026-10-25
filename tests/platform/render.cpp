#include <vultra/assets/asset_source.hpp>
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

void renderMultipassProbe(vultra::Device& device, vultra::AssetSource& assets, const std::filesystem::path& capture);
void renderComputeProbe(vultra::Device& device, vultra::AssetSource& assets, const std::filesystem::path& capture);

namespace
{
#ifndef __ANDROID__
    constexpr unsigned char kProject[] = {
#include "project.vpk.h"
    };
#endif

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

void renderPlatformProbe(std::span<const std::byte> package, const std::filesystem::path& capture)
{
    using namespace vultra;
    AssetSource          assets(VpkArchive("/embedded/project.vpk", package));
    const auto           colorBytes = assets.read("clear.txt");
    std::istringstream   text(std::string(reinterpret_cast<const char*>(colorBytes.data()), colorBytes.size()));
    std::array<float, 4> clear {};
    require(bool(text >> clear[0] >> clear[1] >> clear[2] >> clear[3]), "Invalid packed clear color");
    // Ask VRI to enable the supported subset, then choose paths from granted capabilities, not API names.
    constexpr uint64_t optional = VriFeature_RayTracing | VriFeature_RayQuery | VriFeature_MeshShader;
    Device             device(true, nullptr, optional, true);
    const auto&        capabilities = *device.core.GetDeviceDesc(device.handle);
    require(device.features == capabilities.enabledFeatures, "Device exposed requested rather than granted features");
    const auto api = capabilities.graphicsAPI;
    std::cout << "Platform VRI API: " << int(api) << '\n';
    const auto    colorSupport = device.core.GetFormatSupport(device.handle, VriFormat_RGBA8_UNORM);
    const auto    depthSupport = device.core.GetFormatSupport(device.handle, VriFormat_D32_SFLOAT);
    const bool    multipass    = (colorSupport & VriFormatSupport_Texture) &&
                                 (colorSupport & VriFormatSupport_ColorAttachment) &&
                                 (depthSupport & VriFormatSupport_DepthStencil);
    std::ofstream report(capture.parent_path() / (capture.stem().string() + "-capabilities.json"));
    report << std::boolalpha << "{\n  \"version\": 1,\n  \"api\": " << int(api)
           << ",\n  \"requestedFeatures\": " << optional << ",\n  \"enabledFeatures\": " << device.features
           << ",\n  \"compute\": " << bool(capabilities.hasComputeShader)
           << ",\n  \"rayTracing\": " << bool(capabilities.hasRayTracing)
           << ",\n  \"rayQuery\": " << bool(capabilities.hasRayQuery)
           << ",\n  \"meshShader\": " << bool(capabilities.hasMeshShader) << ",\n  \"multipassRaster\": " << multipass
           << "\n}\n";
    report.close();
    require(bool(report), "Write platform capability report");
    std::cout << "CAPABILITIES: compute=" << int(capabilities.hasComputeShader)
              << ", RT=" << int(capabilities.hasRayTracing) << ", ray query=" << int(capabilities.hasRayQuery)
              << ", mesh=" << int(capabilities.hasMeshShader) << '\n';
    const bool webgpu   = api == VriGraphicsAPI_WebGPU;
    auto       vertex   = assets.read(webgpu ? "vertexMain.wgsl" : "vertexMain.spv");
    auto       fragment = assets.read(webgpu ? "fragmentMain.wgsl" : "fragmentMain.spv");
    if (webgpu)
    {
        // VRI's WGSL entry points consume NUL-terminated text rather than SPIR-V words.
        vertex.push_back(std::byte {0});
        fragment.push_back(std::byte {0});
    }
    const std::array shaders {
        VriShaderDesc {VriShaderStage_Vertex, vertex.data(), vertex.size(), "vertexMain"},
        VriShaderDesc {VriShaderStage_Fragment, fragment.data(), fragment.size(), "fragmentMain"}};
    VriPipelineLayout*          layout = nullptr;
    const VriPipelineLayoutDesc layoutDesc {};
    check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout), "Create probe layout");
    const auto destroyLayout = [&](VriPipelineLayout* value)
    {
        device.core.DestroyPipelineLayout(value);
    };
    const std::unique_ptr<VriPipelineLayout, decltype(destroyLayout)> layoutOwner(layout, destroyLayout);
    VriColorAttachmentDesc                                            color {};
    color.format         = VriFormat_RGBA8_UNORM;
    color.colorWriteMask = VriColorWrite_RGBA;
    VriGraphicsPipelineDesc description {};
    description.pipelineLayout          = layout;
    description.shaders                 = shaders.data();
    description.shaderNum               = shaders.size();
    description.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
    description.rasterization.cullMode  = VriCullMode_None;
    description.rasterization.lineWidth = 1;
    description.multisample.sampleNum   = 1;
    description.outputMerger.colors     = &color;
    description.outputMerger.colorNum   = 1;
    VriPipeline* pipeline               = nullptr;
    check(device.core.CreateGraphicsPipeline(device.handle, &description, &pipeline), "Create probe pipeline");
    const auto destroyPipeline = [&](VriPipeline* value)
    {
        device.core.DestroyPipeline(value);
    };
    const std::unique_ptr<VriPipeline, decltype(destroyPipeline)> pipelineOwner(pipeline, destroyPipeline);
    // Non-aligned width exercises backend readback row packing as well as drawing.
    constexpr Extent size {137, 101};
    Texture          target(device, colorTexture(size, color.format));
    Frame            frame(device);
    for (int i = 0; i < 3; ++i)
    {
        auto* commands = frame.begin();
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        VriAttachmentDesc attachment {};
        attachment.view    = target.view();
        attachment.loadOp  = VriAttachmentLoadOp_Clear;
        attachment.storeOp = VriAttachmentStoreOp_Store;
        std::ranges::copy(clear, attachment.clearValue.color.f32);
        VriAttachmentsDesc rendering {};
        rendering.colors     = &attachment;
        rendering.colorNum   = 1;
        rendering.renderArea = {0, 0, size.width, size.height};
        rendering.layerNum   = 1;
        device.core.CmdBeginRendering(commands, &rendering);
        const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
        const VriRect     scissor {0, 0, size.width, size.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipeline(commands, pipeline);
        const VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(commands, &draw);
        device.core.CmdEndRendering(commands);
        frame.submitAndWait();
    }
    ImageReadback readback(device, target);
    readback.record(frame.begin(), target);
    frame.submitAndWait();
    const auto image  = readback.consume();
    const auto center = imagePixel(image, size.width / 2, size.height / 2);
    const auto corner = imagePixel(image, 0, 0);
    require(center[0] > 0.1f && center[1] > 0.1f && center[2] > 0.1f && center[3] == 1,
            "Packed shaders did not render the colored triangle");
    for (size_t i = 0; i < 4; ++i)
    {
        require(std::abs(corner[i] - clear[i]) <= 1.0f / 255, "GPU clear differs from packed data");
    }
    savePng(image, capture.parent_path() / (capture.stem().string() + "-triangle.png"));
    renderComputeProbe(device, assets, capture.parent_path() / (capture.stem().string() + "-compute.png"));
    if (multipass)
    {
        std::cout << "SCENE_PATH: textured depth-tested raster + postprocess\n";
        renderMultipassProbe(device, assets, capture);
    }
    else
    {
        std::cout << "SCENE_PATH: simple triangle (required color/depth format capabilities unavailable)\n";
        savePng(image, capture);
    }
#ifdef __EMSCRIPTEN__
    EM_ASM(
        {
            const image = document.createElement('img');
            image.src   = URL.createObjectURL(new Blob([FS.readFile(UTF8ToString($0))],
                                                       {
                                                         type:
                                                             'image/png'
                                                     }));
            image.style = 'width:548px;image-rendering:pixelated';
            document.body.appendChild(image);
            document.body.dataset.result = 'passed';
        },
        capture.string().c_str());
#endif
    std::cout << "PLATFORM_RENDER_PASSED: embedded VPK, queried capabilities and selected GPU paths\n";
}

#ifndef __ANDROID__
int main(int argc, char** argv)
try
{
    renderPlatformProbe(std::as_bytes(std::span(kProject)), argc > 1 ? argv[1] : "platform.png");
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
#endif
