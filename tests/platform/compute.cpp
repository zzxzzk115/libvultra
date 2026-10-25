#include <vultra/assets/asset_source.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace vultra;

    struct PatternResources
    {
        Device&            device;
        VriPipelineLayout* layout   = nullptr;
        VriPipeline*       pipeline = nullptr;
        VriDescriptor*     view     = nullptr;
        VriDescriptorPool* pool     = nullptr;
        VriDescriptorSet*  set      = nullptr;

        ~PatternResources()
        {
            if (pipeline)
            {
                device.core.DestroyPipeline(pipeline);
            }
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (view)
            {
                device.core.DestroyDescriptor(view);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };
} // namespace

void renderComputeProbe(vultra::Device& device, vultra::AssetSource& assets, const std::filesystem::path& capture)
{
    using namespace vultra;
    constexpr Extent size {64, 64};
    const auto&      capabilities = *device.core.GetDeviceDesc(device.handle);
    const bool       compute      = capabilities.hasComputeShader;
    const bool       webgpu       = capabilities.graphicsAPI == VriGraphicsAPI_WebGPU;
    auto             outputDesc   = colorTexture(size);
    outputDesc.usage |= VriTextureUsage_TransferDst;
    Texture                      output(device, outputDesc);
    std::unique_ptr<Buffer>      values;
    PatternResources             resources {device};
    Frame                        frame(device);
    VriPipelineLayoutDesc        layout {};
    const VriDescriptorRangeDesc range {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute};
    const VriDescriptorSetDesc   set {0, &range, 1};
    if (compute)
    {
        values = std::make_unique<Buffer>(device,
                                          VriBufferDesc {size.width * size.height * 4,
                                                         4,
                                                         VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                                         VriMemoryLocation_Device});
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages     = VriShaderStage_Compute;
    }
    check(device.core.CreatePipelineLayout(device.handle, &layout, &resources.layout), "Create pattern layout");
    if (compute)
    {
        const VriBufferViewDesc view {values->handle,
                                      VriDescriptorType_StorageBuffer,
                                      VriFormat_Unknown,
                                      0,
                                      values->desc.size};
        check(device.core.CreateBufferView(device.handle, &view, &resources.view), "Create compute output view");
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = 1;
        pool.storageBufferMaxNum = 1;
        check(device.core.CreateDescriptorPool(device.handle, &pool, &resources.pool), "Create compute pool");
        check(device.core.AllocateDescriptorSets(resources.pool, resources.layout, 0, &resources.set, 1),
              "Allocate compute set");
        const VriDescriptor*               descriptor = resources.view;
        const VriDescriptorRangeUpdateDesc update {&descriptor, 1, 0};
        device.core.UpdateDescriptorRanges(resources.set, 0, 1, &update);
        auto code = assets.read(webgpu ? "computeMain.wgsl" : "computeMain.spv");
        if (webgpu)
        {
            code.push_back(std::byte {0});
        }
        const VriShaderDesc    shader {VriShaderStage_Compute, code.data(), code.size(), "computeMain"};
        VriComputePipelineDesc pipeline {};
        pipeline.pipelineLayout = resources.layout;
        pipeline.shader         = shader;
        check(device.core.CreateComputePipeline(device.handle, &pipeline, &resources.pipeline),
              "Create compute pattern pipeline");
    }
    else
    {
        auto vertex   = assets.read(webgpu ? "fallbackVertex.wgsl" : "fallbackVertex.spv");
        auto fragment = assets.read(webgpu ? "fallbackFragment.wgsl" : "fallbackFragment.spv");
        if (webgpu)
        {
            vertex.push_back(std::byte {0});
            fragment.push_back(std::byte {0});
        }
        const std::array shaders {
            VriShaderDesc {VriShaderStage_Vertex, vertex.data(), vertex.size(), "fallbackVertex"},
            VriShaderDesc {VriShaderStage_Fragment, fragment.data(), fragment.size(), "fallbackFragment"}};
        VriColorAttachmentDesc color {};
        color.format         = outputDesc.format;
        color.colorWriteMask = VriColorWrite_RGBA;
        VriGraphicsPipelineDesc pipeline {};
        pipeline.pipelineLayout          = resources.layout;
        pipeline.shaders                 = shaders.data();
        pipeline.shaderNum               = uint32_t(shaders.size());
        pipeline.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
        pipeline.rasterization.cullMode  = VriCullMode_None;
        pipeline.rasterization.lineWidth = 1;
        pipeline.multisample.sampleNum   = 1;
        pipeline.outputMerger.colors     = &color;
        pipeline.outputMerger.colorNum   = 1;
        check(device.core.CreateGraphicsPipeline(device.handle, &pipeline, &resources.pipeline),
              "Create raster pattern fallback");
    }
    auto* commands = frame.begin();
    device.core.CmdSetPipelineLayout(commands, resources.layout);
    if (compute)
    {
        values->transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
        device.core.CmdSetPipeline(commands, resources.pipeline);
        device.core.CmdSetDescriptorSet(commands, 0, resources.set);
        const VriDispatchDesc dispatch {8, 8, 1};
        device.core.CmdDispatch(commands, &dispatch);
        values->transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        output.transition(commands,
                          {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc copy {};
        copy.bufferRowLength   = size.width;
        copy.bufferImageHeight = size.height;
        copy.texture           = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, size.width, size.height, 1};
        device.core.CmdUploadBufferToTexture(commands, output.handle, values->handle, &copy);
    }
    else
    {
        output.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        constexpr std::array<float, 4> clear {};
        beginColorPass(device, commands, output.view(), size, clear.data());
        const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
        const VriRect     scissor {0, 0, size.width, size.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipeline(commands, resources.pipeline);
        const VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(commands, &draw);
        device.core.CmdEndRendering(commands);
    }
    frame.submitAndWait();
    const auto image = readback(device, output);
    savePng(image, capture);
    for (uint32_t y = 0; y < size.height; ++y)
    {
        for (uint32_t x = 0; x < size.width; ++x)
        {
            const std::array<uint32_t, 4> expected {x * 4, y * 4, (x ^ y) * 4, 255};
            const auto                    pixel = imagePixel(image, x, y);
            for (size_t channel = 0; channel < 4; ++channel)
            {
                if (std::abs(pixel[channel] - float(expected[channel]) / 255) > 0.00001f)
                {
                    throw std::runtime_error("Compute/raster pattern differs from its exact CPU reference");
                }
            }
        }
    }
    std::cout << "PATTERN_PATH: " << (compute ? "compute dispatch" : "simple raster (compute unavailable)") << '\n';
}
