#include "../support/check.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numeric>
#include <string>

namespace
{
    using namespace vultra;
    using support::must;
    using support::require;

    constexpr Extent kSize {321, 237};

    struct Vertex
    {
        std::array<float, 3> position;
        std::array<float, 3> normal;
        std::array<float, 2> uv;
    };

    struct Parameters
    {
        std::array<float, 4> transform;
        std::array<float, 4> tint;
        std::array<float, 4> viewport;
    };

    static_assert(sizeof(Vertex) == 32 && sizeof(Parameters) == 48);

    struct DrawResources
    {
        Device&                          device;
        VriPipelineLayout*               layout  = nullptr;
        VriDescriptor*                   sampler = nullptr;
        VriDescriptorPool*               pool    = nullptr;
        std::array<VriDescriptorSet*, 3> sets {};
        std::array<VriDescriptor*, 3>    uniforms {};
        std::array<VriPipeline*, 2>      pipelines {};

        ~DrawResources()
        {
            for (auto* pipeline : pipelines)
            {
                if (pipeline)
                {
                    device.core.DestroyPipeline(pipeline);
                }
            }
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            for (auto* uniform : uniforms)
            {
                if (uniform)
                {
                    device.core.DestroyDescriptor(uniform);
                }
            }
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    void writeBuffer(Device& device, Buffer& buffer, std::span<const std::byte> bytes)
    {
        auto* mapped = device.core.MapBuffer(buffer.handle, 0, bytes.size());
        require(mapped != nullptr, "Map multipass upload buffer");
        std::memcpy(mapped, bytes.data(), bytes.size());
        device.core.UnmapBuffer(buffer.handle);
    }

    std::array<Vertex, 24> cubeVertices()
    {
        constexpr std::array<std::array<float, 3>, 8> corners {
            {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}};
        constexpr std::array<std::array<uint32_t, 4>, 6> faces {
            {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}}};
        constexpr std::array<std::array<float, 3>, 6> normals {
            {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
        constexpr std::array<std::array<float, 2>, 4> uv {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
        std::array<Vertex, 24>                        result {};
        for (size_t face = 0; face < faces.size(); ++face)
        {
            for (size_t corner = 0; corner < 4; ++corner)
            {
                result[face * 4 + corner] = {corners[faces[face][corner]], normals[face], uv[corner]};
            }
        }
        return result;
    }

    std::array<uint16_t, 36> cubeIndices()
    {
        std::array<uint16_t, 36>          result {};
        constexpr std::array<uint16_t, 6> faceIndices {0, 1, 2, 0, 2, 3};
        for (uint16_t face = 0; face < 6; ++face)
        {
            for (size_t i = 0; i < faceIndices.size(); ++i)
            {
                result[face * 6 + i] = uint16_t(face * 4 + faceIndices[i]);
            }
        }
        return result;
    }

    void createDrawResources(DrawResources&         resources,
                             AssetSource&           assets,
                             Texture&               checker,
                             Texture&               scene,
                             std::array<Buffer, 3>& uniforms)
    {
        auto&            device = resources.device;
        const std::array ranges {VriDescriptorRangeDesc {0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
                                 VriDescriptorRangeDesc {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment},
                                 VriDescriptorRangeDesc {2,
                                                         1,
                                                         VriDescriptorType_ConstantBuffer,
                                                         VriShaderStage_Vertex | VriShaderStage_Fragment}};
        const VriDescriptorSetDesc set {0, ranges.data(), uint32_t(ranges.size())};
        VriPipelineLayoutDesc      layout {};
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
        must(check(device.core.CreatePipelineLayout(device.handle, &layout, &resources.layout),
                   "Create multipass layout"));
        VriSamplerDesc sampler {};
        sampler.minFilter    = VriFilter_Nearest;
        sampler.magFilter    = VriFilter_Nearest;
        sampler.addressModeU = VriAddressMode_ClampToEdge;
        sampler.addressModeV = VriAddressMode_ClampToEdge;
        sampler.addressModeW = VriAddressMode_ClampToEdge;
        must(check(device.core.CreateSampler(device.handle, &sampler, &resources.sampler), "Create multipass sampler"));
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = 3;
        pool.textureMaxNum        = 3;
        pool.samplerMaxNum        = 3;
        pool.constantBufferMaxNum = 3;
        must(check(device.core.CreateDescriptorPool(device.handle, &pool, &resources.pool), "Create multipass pool"));
        must(check(device.core.AllocateDescriptorSets(resources.pool, resources.layout, 0, resources.sets.data(), 3),
                   "Allocate multipass sets"));
        for (size_t i = 0; i < uniforms.size(); ++i)
        {
            const VriBufferViewDesc view {uniforms[i].handle,
                                          VriDescriptorType_ConstantBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          sizeof(Parameters)};
            must(check(device.core.CreateBufferView(device.handle, &view, &resources.uniforms[i]),
                       "Create multipass uniform"));
            const std::array<const VriDescriptor*, 3>   descriptors {must(i == 2 ? scene.view() : checker.view()),
                                                                     resources.sampler,
                                                                     resources.uniforms[i]};
            std::array<VriDescriptorRangeUpdateDesc, 3> updates {};
            for (size_t j = 0; j < updates.size(); ++j)
            {
                updates[j].descriptors   = &descriptors[j];
                updates[j].descriptorNum = 1;
            }
            device.core.UpdateDescriptorRanges(resources.sets[i], 0, uint32_t(updates.size()), updates.data());
        }
        const bool       webgpu = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
        const std::array entries {"sceneVertex", "sceneFragment", "postVertex", "postFragment"};
        const VriVertexStreamDesc stream {sizeof(Vertex), 0, VriVertexStepRate_PerVertex};
        const std::array          attributes {
            VriVertexAttributeDesc {VriFormat_RGB32_SFLOAT, offsetof(Vertex, position), 0, "POSITION", 0},
            VriVertexAttributeDesc {VriFormat_RGB32_SFLOAT, offsetof(Vertex, normal), 0, "NORMAL", 0},
            VriVertexAttributeDesc {VriFormat_RG32_SFLOAT, offsetof(Vertex, uv), 0, "TEXCOORD", 0}};
        VriColorAttachmentDesc color {};
        color.format         = VriFormat_RGBA8_UNORM;
        color.colorWriteMask = VriColorWrite_RGBA;
        for (size_t i = 0; i < resources.pipelines.size(); ++i)
        {
            const auto extension = webgpu ? ".wgsl" : ".spv";
            auto       vertex    = must(assets.read(std::string(entries[i * 2]) + extension));
            auto       fragment  = must(assets.read(std::string(entries[i * 2 + 1]) + extension));
            if (webgpu)
            {
                vertex.push_back(std::byte {0});
                fragment.push_back(std::byte {0});
            }
            const std::array shaders {
                VriShaderDesc {VriShaderStage_Vertex, vertex.data(), vertex.size(), entries[i * 2]},
                VriShaderDesc {VriShaderStage_Fragment, fragment.data(), fragment.size(), entries[i * 2 + 1]}};
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
            if (i == 0)
            {
                pipeline.vertexInput                     = {attributes.data(), uint32_t(attributes.size()), &stream, 1};
                pipeline.depthStencil.depthTest          = true;
                pipeline.depthStencil.depthWrite         = true;
                pipeline.depthStencil.depthCompareOp     = VriCompareOp_Less;
                pipeline.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
            }
            must(check(device.core.CreateGraphicsPipeline(device.handle, &pipeline, &resources.pipelines[i]),
                       "Create multipass pipeline"));
        }
    }

    void renderScene(DrawResources&    resources,
                     VriCommandBuffer* commands,
                     Texture&          color,
                     Texture&          depth,
                     Buffer&           vertices,
                     Buffer&           indices,
                     bool              reverse)
    {
        auto& device = resources.device;
        color.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        depth.transition(commands,
                         {VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite,
                          VriLayout_DepthStencilAttachment,
                          VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
        VriAttachmentDesc colorAttachment {};
        colorAttachment.view                    = must(color.view());
        colorAttachment.loadOp                  = VriAttachmentLoadOp_Clear;
        colorAttachment.storeOp                 = VriAttachmentStoreOp_Store;
        colorAttachment.clearValue.color.f32[0] = 0.035f;
        colorAttachment.clearValue.color.f32[1] = 0.055f;
        colorAttachment.clearValue.color.f32[2] = 0.085f;
        colorAttachment.clearValue.color.f32[3] = 1;
        VriAttachmentDesc depthAttachment {};
        depthAttachment.view                          = must(depth.view());
        depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
        depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
        depthAttachment.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.colors     = &colorAttachment;
        attachments.colorNum   = 1;
        attachments.depth      = &depthAttachment;
        attachments.renderArea = {0, 0, kSize.width, kSize.height};
        attachments.layerNum   = 1;
        device.core.CmdBeginRendering(commands, &attachments);
        const VriViewport viewport {0, 0, float(kSize.width), float(kSize.height), 0, 1};
        const VriRect     scissor {0, 0, kSize.width, kSize.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipelineLayout(commands, resources.layout);
        device.core.CmdSetPipeline(commands, resources.pipelines[0]);
        const VriVertexBufferBinding binding {vertices.handle, 0};
        device.core.CmdSetVertexBuffers(commands, 0, &binding, 1);
        device.core.CmdSetIndexBuffer(commands, indices.handle, 0, VriIndexType_UInt16);
        const VriDrawIndexedDesc draw {36, 1, 0, 0, 0};
        for (uint32_t i = 0; i < 2; ++i)
        {
            device.core.CmdSetDescriptorSet(commands, 0, resources.sets[reverse ? 1 - i : i]);
            device.core.CmdDrawIndexed(commands, &draw);
        }
        device.core.CmdEndRendering(commands);
    }

    void renderPost(DrawResources& resources, VriCommandBuffer* commands, Texture& scene, Texture& output)
    {
        auto& device = resources.device;
        scene.transition(commands,
                         {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        output.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        constexpr std::array<float, 4> clear {};
        beginColorPass(device, commands, must(output.view()), kSize, clear.data());
        const VriViewport viewport {0, 0, float(kSize.width), float(kSize.height), 0, 1};
        const VriRect     scissor {0, 0, kSize.width, kSize.height};
        device.core.CmdSetViewports(commands, &viewport, 1);
        device.core.CmdSetScissors(commands, &scissor, 1);
        device.core.CmdSetPipelineLayout(commands, resources.layout);
        device.core.CmdSetPipeline(commands, resources.pipelines[1]);
        device.core.CmdSetDescriptorSet(commands, 0, resources.sets[2]);
        const VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(commands, &draw);
        device.core.CmdEndRendering(commands);
    }

    std::expected<void, Error> verifyPost(const Image& scene, const Image& post, float gain)
    {
        for (uint32_t y = 0; y < kSize.height; ++y)
        {
            for (uint32_t x = 0; x < kSize.width; ++x)
            {
                const std::array uv {float(x + 0.5f) / kSize.width, float(y + 0.5f) / kSize.height, 0.25f};
                const size_t     offset = (size_t(y) * kSize.width + x) * 4;
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    const float expected = scene.rgba[offset + channel] * gain + uv[channel] * 0.05f;
                    if (std::abs(post.rgba[offset + channel] - expected) > 1.0f / 255)
                    {
                        return makeError(
                            "Postprocess differs from CPU reference at {},{} channel {}: expected {}, got {}",
                            x,
                            y,
                            channel,
                            expected,
                            post.rgba[offset + channel]);
                    }
                }
                if (post.rgba[offset + 3] != 1)
                {
                    return makeError("Postprocess alpha is not opaque");
                }
            }
        }
        return {};
    }
} // namespace

void renderMultipassProbe(vultra::Device& device, vultra::AssetSource& assets, const std::filesystem::path& capture)
{
    using namespace vultra;
    const auto vertices     = cubeVertices();
    const auto indices      = cubeIndices();
    auto       vertexBuffer = must(
        Buffer::create(device,
                       {sizeof(vertices), sizeof(Vertex), VriBufferUsage_VertexBuffer, VriMemoryLocation_HostUpload}));
    auto indexBuffer = must(
        Buffer::create(device,
                       {sizeof(indices), sizeof(uint16_t), VriBufferUsage_IndexBuffer, VriMemoryLocation_HostUpload}));
    writeBuffer(device, vertexBuffer, std::as_bytes(std::span(vertices)));
    writeBuffer(device, indexBuffer, std::as_bytes(std::span(indices)));
    const auto pixels = must(assets.read("checker.rgba"));
    require(pixels.size() == 16 * 16 * 4, "Packed checker texture has invalid dimensions");
    const auto alignment =
        std::lcm(4u, std::max(1u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment));
    const auto             rowBytes = (64 + alignment - 1) / alignment * alignment;
    std::vector<std::byte> padded(size_t(rowBytes) * 16);
    for (size_t row = 0; row < 16; ++row)
    {
        std::memcpy(padded.data() + row * rowBytes, pixels.data() + row * 64, 64);
    }
    auto staging =
        must(Buffer::create(device, {padded.size(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload}));
    writeBuffer(device, staging, padded);
    auto checkerDesc  = colorTexture({16, 16});
    checkerDesc.usage = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
    auto                  checker = must(Texture::create(device, checkerDesc));
    auto                  scene   = must(Texture::create(device, colorTexture(kSize)));
    auto                  depth   = must(Texture::create(device, depthTexture(kSize), nullptr, VriImageAspect_Depth));
    auto                  output  = must(Texture::create(device, colorTexture(kSize)));
    const VriBufferDesc   uniformDesc {256, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload};
    std::array<Buffer, 3> uniforms {must(Buffer::create(device, uniformDesc)),
                                    must(Buffer::create(device, uniformDesc)),
                                    must(Buffer::create(device, uniformDesc))};
    DrawResources         resources {device};
    // Frame retires before descriptors/buffers/textures.
    const auto frame = must(Frame::create(device));
    createDrawResources(resources, assets, checker, scene, uniforms);
    auto* commands = must(frame->begin());
    checker.transition(commands,
                       {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
    VriBufferTextureCopyDesc copy {};
    copy.bufferRowLength   = rowBytes / 4;
    copy.bufferImageHeight = 16;
    copy.texture           = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 16, 16, 1};
    device.core.CmdUploadBufferToTexture(commands, checker.handle, staging.handle, &copy);
    checker.transition(commands,
                       {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
    must(frame->submitAndWait());
    const auto sibling = [&](const char* suffix)
    {
        return capture.parent_path() / (capture.stem().string() + suffix + ".png");
    };
    const auto uploaded = must(readback(device, checker));
    must(savePng(uploaded, sibling("-texture")));
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        const auto expected = std::to_integer<uint8_t>(pixels[i]);
        if (std::abs(uploaded.rgba[i] - float(expected) / 255) >= 0.00001f)
        {
            support::fail(std::format("Texture upload differs at channel {}: expected {}, got {}",
                                      i,
                                      expected,
                                      uploaded.rgba[i]));
        }
    }
    Image              sceneImage;
    Image              postImage;
    std::vector<float> previous;
    for (uint32_t step = 0; step < 3; ++step)
    {
        const float                     gain = 0.65f + float(step) * 0.15f;
        const std::array<Parameters, 3> parameters {{{{0.25f + step * 0.2f, -0.35f, 3.0f, 0.65f},
                                                      {1, 0.3f, 0.12f, 1},
                                                      {float(kSize.width) / kSize.height, gain, 0, 0}},
                                                     {{-0.4f - step * 0.1f, 0.6f, 4.1f, 0.8f},
                                                      {0.12f, 0.45f, 1, 1},
                                                      {float(kSize.width) / kSize.height, gain, 0, 0}},
                                                     {{}, {}, {float(kSize.width) / kSize.height, gain, 0, 0}}}};
        // The previous submission/readback completed before these host-visible buffers are reused.
        for (size_t i = 0; i < uniforms.size(); ++i)
        {
            writeBuffer(device, uniforms[i], std::as_bytes(std::span(&parameters[i], 1)));
        }
        commands = must(frame->begin());
        vertexBuffer.transition(commands, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        indexBuffer.transition(commands, {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        for (auto& uniform : uniforms)
        {
            uniform.transition(
                commands,
                {VriAccess_ConstantBufferRead, VriPipelineStage_VertexShader | VriPipelineStage_FragmentShader});
        }
        renderScene(resources, commands, scene, depth, vertexBuffer, indexBuffer, false);
        renderPost(resources, commands, scene, output);
        must(frame->submitAndWait());
        sceneImage = must(readback(device, scene));
        postImage  = must(readback(device, output));
        if (const auto verified = verifyPost(sceneImage, postImage, gain); !verified)
        {
            must(savePng(sceneImage, sibling("-failed-scene")));
            must(savePng(postImage, sibling("-failed-post")));
            support::fail(verified.error().message);
        }
        const auto center = must(imagePixel(sceneImage, kSize.width / 2, kSize.height / 2));
        require(center[0] > center[2] * 3 && center[0] > 0.05f,
                "Rear cube overwrote the nearer cube: depth occlusion failed");
        if (!previous.empty())
        {
            require(previous != sceneImage.rgba, "Changing the rotation uniform did not change rendered pixels");
        }
        previous = sceneImage.rgba;
    }
    must(savePng(sceneImage, sibling("-scene")));
    must(savePng(postImage, capture));
    // Correct depth testing must make draw order irrelevant, not merely produce nonempty pixels.
    commands = must(frame->begin());
    renderScene(resources, commands, scene, depth, vertexBuffer, indexBuffer, true);
    must(frame->submitAndWait());
    const auto reversed = must(readback(device, scene));
    require(reversed.rgba == sceneImage.rgba, "Reversing draw order changed depth-tested geometry");
}
