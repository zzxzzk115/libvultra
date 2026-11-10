#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>

namespace
{
    using namespace vultra;
    using Pixel = std::array<float, 4>;
    using Words = std::array<uint32_t, 4>;
    constexpr std::array entries {"sample1D",
                                  "sample1DArray",
                                  "sample2D",
                                  "sample2DArray",
                                  "sample3D",
                                  "sampleCube",
                                  "sampleCubeArray",
                                  "sampleDepth",
                                  "sampleStencil",
                                  "sampleMS",
                                  "sampleMSArray"};

    Pixel pixelValue(uint32_t mip, uint32_t layer, uint32_t z)
    {
        const auto value = float(1 + mip * 1000 + layer * 100 + z * 10);
        return {value, value + 1, value + 2, value + 3};
    }

    struct Probe
    {
        Device&                                                     device;
        std::unique_ptr<Frame>                                      frame;
        std::unique_ptr<Buffer>                                     output;
        std::unique_ptr<Buffer>                                     readback;
        VriPipelineLayout*                                          layout     = nullptr;
        VriDescriptorPool*                                          pool       = nullptr;
        VriDescriptorSet*                                           set        = nullptr;
        VriDescriptor*                                              sampler    = nullptr;
        VriDescriptor*                                              outputView = nullptr;
        std::array<std::unique_ptr<ShaderPipeline>, entries.size()> pipelines;
        uint32_t                                                    samples = 0;

        ~Probe()
        {
            device.waitIdleForDestruction();
            for (auto& pipeline : pipelines)
            {
                pipeline.reset();
            }
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
            }
            if (outputView)
            {
                device.core.DestroyDescriptor(outputView);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }

        std::expected<void, std::string> initialize(const ShaderProgram& program)
        {
            auto createdFrame  = Frame::create(device);
            auto createdOutput = Buffer::create(
                device,
                {32, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
            auto createdReadback =
                Buffer::create(device, {32, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!createdFrame || !createdOutput || !createdReadback)
            {
                return std::unexpected("Create texture-view sampling resources failed");
            }
            frame    = std::move(*createdFrame);
            output   = std::move(*createdOutput);
            readback = std::move(*createdReadback);
            std::array<VriDescriptorRangeDesc, 13> ranges {};
            for (uint32_t i = 0; i < 11; ++i)
            {
                ranges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Compute};
            }
            ranges[11] = {11, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute};
            ranges[12] = {12, 1, VriDescriptorType_Sampler, VriShaderStage_Compute};
            const VriDescriptorSetDesc descriptorSet {0, ranges.data(), uint32_t(ranges.size())};
            const VriPushConstantDesc  constants {0, 16, VriShaderStage_Compute};
            VriPipelineLayoutDesc      description {};
            description.descriptorSets   = &descriptorSet;
            description.descriptorSetNum = 1;
            description.pushConstants    = &constants;
            description.pushConstantNum  = 1;
            description.shaderStages     = VriShaderStage_Compute;
            if (device.core.CreatePipelineLayout(device.handle, &description, &layout) != VriResult_Success)
            {
                return std::unexpected("Create texture-view pipeline layout failed");
            }
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum = 1;
            poolDesc.textureMaxNum       = 11;
            poolDesc.storageBufferMaxNum = 1;
            poolDesc.samplerMaxNum       = 1;
            if (device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool) != VriResult_Success ||
                device.core.AllocateDescriptorSets(pool, layout, 0, &set, 1) != VriResult_Success)
            {
                return std::unexpected("Create texture-view descriptors failed");
            }
            VriSamplerDesc samplerDesc {};
            samplerDesc.minFilter    = VriFilter_Nearest;
            samplerDesc.magFilter    = VriFilter_Nearest;
            samplerDesc.addressModeU = VriAddressMode_ClampToEdge;
            samplerDesc.addressModeV = VriAddressMode_ClampToEdge;
            samplerDesc.addressModeW = VriAddressMode_ClampToEdge;
            samplerDesc.maxLod       = 16;
            const VriBufferViewDesc bufferView {output->handle,
                                                VriDescriptorType_StorageBuffer,
                                                VriFormat_Unknown,
                                                0,
                                                32};
            if (device.core.CreateSampler(device.handle, &samplerDesc, &sampler) != VriResult_Success ||
                device.core.CreateBufferView(device.handle, &bufferView, &outputView) != VriResult_Success)
            {
                return std::unexpected("Create texture-view sampler/output view failed");
            }
            for (size_t i = 0; i < entries.size(); ++i)
            {
                auto pipeline = ShaderPipeline::create(
                    device,
                    program,
                    {{entries[i], VriShaderStage_Compute}},
                    [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                    {
                        const VriComputePipelineDesc desc {layout, shaders.front(), device.pipelineCache};
                        VriPipeline*                 handle = nullptr;
                        const auto result = device.core.CreateComputePipeline(device.handle, &desc, &handle);
                        if (result != VriResult_Success || !handle)
                        {
                            if (handle)
                            {
                                device.core.DestroyPipeline(handle);
                            }
                            return std::unexpected("Create texture-view sampling pipeline failed: " +
                                                   std::to_string(result));
                        }
                        return handle;
                    });
                if (!pipeline)
                {
                    return std::unexpected(std::string(entries[i]) + ": " + pipeline.error());
                }
                pipelines[i] = std::move(*pipeline);
            }
            return {};
        }

        std::expected<void, std::string> sample(Texture&           texture,
                                                VriTextureViewDesc view,
                                                uint32_t           entry,
                                                Words              parameters,
                                                Words              expected,
                                                Words              dimensions)
        {
            auto descriptor = texture.viewExpected(view);
            if (!descriptor)
            {
                return std::unexpected(std::string(entries[entry]) + ": " + descriptor.error());
            }
            std::array<const VriDescriptor*, 13> descriptors {};
            std::fill_n(descriptors.begin(), 11, *descriptor);
            descriptors[11] = outputView;
            descriptors[12] = sampler;
            std::array<VriDescriptorRangeUpdateDesc, 13> updates {};
            for (size_t i = 0; i < updates.size(); ++i)
            {
                updates[i] = {&descriptors[i], 1};
            }
            device.core.UpdateDescriptorRanges(set, 0, uint32_t(updates.size()), updates.data());
            auto commands = frame->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            texture.transition(
                *commands,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
            output->transition(*commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            device.core.CmdSetPipelineLayout(*commands, layout);
            device.core.CmdSetPipeline(*commands, pipelines[entry]->handle());
            device.core.CmdSetDescriptorSet(*commands, 0, set);
            device.core.CmdSetConstants(*commands, 0, parameters.data(), sizeof(parameters));
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            output->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            readback->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferCopyDesc copy {};
            copy.size = 32;
            device.core.CmdCopyBuffer(*commands, readback->handle, output->handle, &copy);
            if (auto submitted = frame->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const void* data = device.core.MapBuffer(readback->handle, 0, 32);
            if (!data)
            {
                return std::unexpected("Map texture-view sampling readback failed");
            }
            std::array<Words, 2> actual {};
            std::memcpy(actual.data(), data, sizeof(actual));
            device.core.UnmapBuffer(readback->handle);
            if (actual[0] != expected || actual[1] != dimensions)
            {
                std::fprintf(stderr,
                             "%s viewMip=%u viewLayer=%u lod=%u layer=%u: value=%u,%u,%u,%u "
                             "expected=%u,%u,%u,%u dimensions=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                             entries[entry],
                             view.baseMip,
                             view.baseLayer,
                             parameters[1],
                             parameters[0],
                             actual[0][0],
                             actual[0][1],
                             actual[0][2],
                             actual[0][3],
                             expected[0],
                             expected[1],
                             expected[2],
                             expected[3],
                             actual[1][0],
                             actual[1][1],
                             actual[1][2],
                             actual[1][3],
                             dimensions[0],
                             dimensions[1],
                             dimensions[2],
                             dimensions[3]);
                return std::unexpected("Texture-view sampling or dimensions differ from the uploaded subresource");
            }
            ++samples;
            return {};
        }
    };

    std::expected<void, std::string> upload(Probe& probe, Texture& texture)
    {
        struct Region
        {
            VriBufferTextureCopyDesc copy {};
            uint64_t                 rowBytes = 0;
        };

        const auto* caps = probe.device.core.GetDeviceDesc(probe.device.handle);
        if (!caps)
        {
            return std::unexpected("Texture-view upload capabilities unavailable");
        }
        const auto rowAlignment =
            std::lcm(uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 256u)), uint64_t(16));
        std::vector<Region> regions;
        uint64_t            bytes = 0;
        for (uint32_t layer = 0; layer < texture.desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < texture.desc.mipNum; ++mip)
            {
                Region region;
                auto&  image                  = region.copy.texture;
                image                         = {mip,
                                                 layer,
                                                 1,
                                                 VriImageAspect_Color,
                                                 0,
                                                 0,
                                                 0,
                                                 std::max(texture.desc.width >> mip, 1u),
                                                 std::max(texture.desc.height >> mip, 1u),
                                                 std::max(texture.desc.depth >> mip, 1u)};
                region.rowBytes               = (image.width * 16 + rowAlignment - 1) / rowAlignment * rowAlignment;
                region.copy.bufferOffset      = bytes;
                region.copy.bufferRowLength   = uint32_t(region.rowBytes / 16);
                region.copy.bufferImageHeight = image.height;
                bytes += region.rowBytes * image.height * image.depth;
                regions.push_back(region);
            }
        }
        auto staging =
            Buffer::create(probe.device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!staging)
        {
            return std::unexpected(staging.error());
        }
        auto* data = static_cast<std::byte*>(probe.device.core.MapBuffer((*staging)->handle, 0, bytes));
        if (!data)
        {
            return std::unexpected("Map texture-view upload failed");
        }
        std::memset(data, 0, size_t(bytes));
        for (const auto& region : regions)
        {
            const auto& image = region.copy.texture;
            for (uint32_t z = 0; z < image.depth; ++z)
            {
                const auto pixel = pixelValue(image.mip, image.baseLayer, z);
                for (uint32_t y = 0; y < image.height; ++y)
                {
                    for (uint32_t x = 0; x < image.width; ++x)
                    {
                        std::memcpy(data + region.copy.bufferOffset +
                                        (uint64_t(z) * image.height + y) * region.rowBytes + x * 16,
                                    pixel.data(),
                                    sizeof(pixel));
                    }
                }
            }
        }
        probe.device.core.UnmapBuffer((*staging)->handle);
        auto commands = probe.frame->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        texture.transition(*commands,
                           {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        (*staging)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        for (const auto& region : regions)
        {
            probe.device.core.CmdUploadBufferToTexture(*commands, texture.handle, (*staging)->handle, &region.copy);
        }
        return probe.frame->submitAndWaitExpected();
    }

    std::expected<void, std::string> verifyView(Probe& probe, Texture& texture, VriTextureViewDesc view, uint32_t entry)
    {
        const uint32_t mips   = view.mipNum ? view.mipNum : texture.desc.mipNum - view.baseMip;
        const uint32_t layers = view.layerNum ? view.layerNum : texture.desc.layerNum - view.baseLayer;
        for (uint32_t mip = 0; mip < mips; ++mip)
        {
            const uint32_t actualMip = view.baseMip + mip;
            const uint32_t depth     = std::max(texture.desc.depth >> actualMip, 1u);
            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                for (uint32_t z = 0; z < depth; ++z)
                {
                    const Words dimensions {std::max(texture.desc.width >> actualMip, 1u),
                                            std::max(texture.desc.height >> actualMip, 1u),
                                            entry == 4 ? depth : layers,
                                            mips};
                    if (auto result =
                            probe.sample(texture,
                                         view,
                                         entry,
                                         {layer, mip, z, 0},
                                         std::bit_cast<Words>(pixelValue(actualMip, view.baseLayer + layer, z)),
                                         dimensions);
                        !result)
                    {
                        return result;
                    }
                }
            }
        }
        return {};
    }

    std::expected<void, std::string> verifyColorViews(Probe& probe)
    {
        const std::array types {VriTextureType_1D,
                                VriTextureType_1DArray,
                                VriTextureType_2D,
                                VriTextureType_2DArray,
                                VriTextureType_3D,
                                VriTextureType_Cube,
                                VriTextureType_CubeArray};
        for (uint32_t type = 0; type < types.size(); ++type)
        {
            auto desc     = colorTexture({8, type < 2 ? 1u : 8u}, VriFormat_RGBA32_SFLOAT);
            desc.type     = types[type];
            desc.mipNum   = type < 2 ? 1 : 3;
            desc.layerNum = type == 1 || type == 3 ? 3u : 1u;
            if (type == 5)
            {
                desc.layerNum = 6;
            }
            if (type == 6)
            {
                desc.layerNum = 12;
            }
            desc.depth   = type == 4 ? 4 : 1;
            desc.usage   = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
            auto texture = Texture::create(probe.device, desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            if (auto result = upload(probe, **texture); !result)
            {
                return result;
            }
            VriTextureViewDesc whole {};
            whole.viewType              = VriTextureViewType_MaxEnum;
            auto               implicit = (*texture)->viewExpected();
            auto               selected = (*texture)->viewExpected(whole);
            VriTextureViewDesc explicitView {(*texture)->handle,
                                             VriTextureViewType(type),
                                             desc.format,
                                             VriImageAspect_Color,
                                             0,
                                             desc.mipNum,
                                             0,
                                             desc.layerNum};
            auto               concrete = (*texture)->viewExpected(explicitView);
            if (!implicit || !selected || !concrete || *implicit != *selected || *implicit != *concrete)
            {
                return std::unexpected("Equivalent texture view descriptions did not reuse the descriptor");
            }
            if (auto result = verifyView(probe, **texture, whole, type); !result)
            {
                return result;
            }
            auto subset      = whole;
            subset.baseMip   = desc.mipNum > 1 ? 1 : 0;
            subset.baseLayer = type == 1 || type == 3 ? 1u : 0u;
            if (type == 6)
            {
                subset.baseLayer = 6;
            }
            if (auto result = verifyView(probe, **texture, subset, type); !result)
            {
                return result;
            }
            if (type == 3 || type >= 5)
            {
                auto slice      = explicitView;
                slice.viewType  = VriTextureViewType_2D;
                slice.baseMip   = 2;
                slice.mipNum    = 1;
                slice.baseLayer = type == 6 ? 7u : 2u;
                slice.layerNum  = 1;
                if (auto result = verifyView(probe, **texture, slice, 2); !result)
                {
                    return result;
                }
            }
            if (type == 6)
            {
                auto cube     = subset;
                cube.viewType = VriTextureViewType_Cube;
                cube.layerNum = 6;
                if (auto result = verifyView(probe, **texture, cube, 5); !result)
                {
                    return result;
                }
            }
            std::printf("VIEW_COLOR type=%u samples=%u passed\n", type, probe.samples);
            std::fflush(stdout);
        }
        return {};
    }

    std::expected<void, std::string> verifyAttachments(Probe& probe, VriFormat format, uint32_t layers)
    {
        const bool depth = format == VriFormat_D32_SFLOAT || format == VriFormat_D32_SFLOAT_S8_UINT;
        auto       desc  = colorTexture({8, 8}, format);
        desc.type        = layers > 1 ? VriTextureType_2DArray : VriTextureType_2D;
        desc.layerNum    = layers;
        desc.mipNum      = depth ? 2 : 1;
        desc.sampleNum   = depth ? 1 : 4;
        desc.usage       = VriTextureUsage_ShaderResource |
                     (depth ? VriTextureUsage_DepthStencilAttachment : VriTextureUsage_ColorAttachment);
        const auto colorViewType = layers > 1 ? VriTextureViewType_2DArray : VriTextureViewType_2D;
        auto       texture       = Texture::create(probe.device, desc);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        auto commands = probe.frame->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        (*texture)->transition(*commands,
                               {depth ? VriAccess_DepthStencilAttachmentWrite : VriAccess_ColorAttachmentWrite,
                                depth ? VriLayout_DepthStencilAttachment : VriLayout_ColorAttachment,
                                VriPipelineStage_AllCommands});
        for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
        {
            for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
            {
                VriTextureViewDesc view {(*texture)->handle,
                                         depth ? VriTextureViewType_2D : colorViewType,
                                         VriFormat_Unknown,
                                         VriImageAspect_None,
                                         mip,
                                         1,
                                         layer,
                                         1};
                auto               selected = (*texture)->viewExpected(view);
                if (!selected)
                {
                    return std::unexpected(selected.error());
                }
                VriAttachmentDesc attachment {};
                attachment.view    = *selected;
                attachment.loadOp  = VriAttachmentLoadOp_Clear;
                attachment.storeOp = VriAttachmentStoreOp_Store;
                if (depth)
                {
                    attachment.clearValue.depthStencil = {float(1 + mip * 2 + layer) * 0.25f,
                                                          (1 + mip * 2 + layer) * 17};
                }
                else
                {
                    const Pixel color {float(layer), float(1 - layer), float(layer), 1};
                    std::copy(color.begin(), color.end(), attachment.clearValue.color.f32);
                }
                VriAttachmentsDesc attachments {};
                attachments.renderArea = {0, 0, desc.width >> mip, desc.height >> mip};
                attachments.layerNum   = 1;
                attachments.depth      = depth ? &attachment : nullptr;
                attachments.stencil    = format == VriFormat_D32_SFLOAT_S8_UINT ? &attachment : nullptr;
                attachments.colors     = depth ? nullptr : &attachment;
                attachments.colorNum   = depth ? 0 : 1;
                probe.device.core.CmdBeginRendering(*commands, &attachments);
                probe.device.core.CmdEndRendering(*commands);
            }
        }
        if (auto result = probe.frame->submitAndWaitExpected(); !result)
        {
            return result;
        }
        for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
        {
            for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
            {
                VriTextureViewDesc view {(*texture)->handle,
                                         depth ? VriTextureViewType_2D : colorViewType,
                                         VriFormat_Unknown,
                                         VriImageAspect_None,
                                         mip,
                                         1,
                                         layer,
                                         1};
                const Words        dimensions {desc.width >> mip, desc.height >> mip, 1, depth ? 1u : 4u};
                if (depth)
                {
                    view.aspect      = VriImageAspect_Depth;
                    const auto color = std::bit_cast<Words>(Pixel {float(1 + mip * 2 + layer) * 0.25f, 0, 0, 1});
                    if (auto result = probe.sample(**texture, view, 7, {}, color, dimensions); !result)
                    {
                        return result;
                    }
                    if (format == VriFormat_D32_SFLOAT_S8_UINT)
                    {
                        view.aspect = VriImageAspect_Stencil;
                        if (auto result =
                                probe.sample(**texture, view, 8, {}, {(1 + mip * 2 + layer) * 17, 0, 0, 1}, dimensions);
                            !result)
                        {
                            return result;
                        }
                    }
                }
                else
                {
                    const auto color = std::bit_cast<Words>(Pixel {float(layer), float(1 - layer), float(layer), 1});
                    for (uint32_t sample = 0; sample < 4; ++sample)
                    {
                        if (auto result = probe.sample(**texture,
                                                       view,
                                                       layers > 1 ? 10u : 9u,
                                                       {0, 0, sample, 0},
                                                       color,
                                                       dimensions);
                            !result)
                        {
                            return result;
                        }
                    }
                }
            }
        }
        if (!depth)
        {
            VriTextureViewDesc whole {};
            whole.viewType = VriTextureViewType_MaxEnum;
            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                const auto color = std::bit_cast<Words>(Pixel {float(layer), float(1 - layer), float(layer), 1});
                for (uint32_t sample = 0; sample < 4; ++sample)
                {
                    if (auto result = probe.sample(**texture,
                                                   whole,
                                                   layers > 1 ? 10u : 9u,
                                                   {layer, 0, sample, 0},
                                                   color,
                                                   {desc.width, desc.height, layers, 4});
                        !result)
                    {
                        return result;
                    }
                }
            }
        }
        std::printf("VIEW_ATTACHMENT format=%u layers=%u samples=%u passed\n", uint32_t(format), layers, probe.samples);
        std::fflush(stdout);
        return {};
    }

    std::expected<void, std::string> verifyStencilOperations(Probe& probe, uint32_t layers)
    {
        auto desc     = colorTexture({8, 8}, VriFormat_D32_SFLOAT_S8_UINT);
        desc.type     = layers > 1 ? VriTextureType_2DArray : VriTextureType_2D;
        desc.layerNum = layers;
        desc.mipNum   = 2;
        desc.usage    = VriTextureUsage_ShaderResource | VriTextureUsage_DepthStencilAttachment;
        auto texture  = Texture::create(probe.device, desc);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        constexpr std::array<uint32_t, 7> stencilBases {17, 17, 65, 127, 201, 201, 201};
        // Initialize both, clear depth while preserving stencil, clear stencil while preserving depth,
        // bind only stencil, verify the combined-depth convenience, then discard each plane independently.
        for (uint32_t stage = 0; stage < stencilBases.size(); ++stage)
        {
            auto commands = probe.frame->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*texture)->transition(*commands,
                                   {VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite,
                                    VriLayout_DepthStencilAttachment,
                                    VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
            {
                for (uint32_t layer = 0; layer < layers; ++layer)
                {
                    VriTextureViewDesc view {(*texture)->handle,
                                             VriTextureViewType_2D,
                                             VriFormat_Unknown,
                                             VriImageAspect_Depth | VriImageAspect_Stencil,
                                             mip,
                                             1,
                                             layer,
                                             1};
                    auto               selected = (*texture)->viewExpected(view);
                    if (!selected)
                    {
                        return std::unexpected(selected.error());
                    }
                    const uint32_t    cell = mip * layers + layer;
                    VriAttachmentDesc depth {};
                    depth.view = *selected;
                    depth.loadOp =
                        stage == 2 || stage == 3 || stage == 5 ? VriAttachmentLoadOp_Load : VriAttachmentLoadOp_Clear;
                    depth.storeOp = stage == 5 ? VriAttachmentStoreOp_DontCare : VriAttachmentStoreOp_Store;
                    depth.clearValue.depthStencil = {float(stage == 1 ? 4 - cell : 1 + cell) * .25f,
                                                     stage == 4 ? 201 + cell : 3u};
                    VriAttachmentDesc stencil     = depth;
                    stencil.loadOp =
                        stage == 1 || stage == 5 || stage == 6 ? VriAttachmentLoadOp_Load : VriAttachmentLoadOp_Clear;
                    stencil.storeOp = stage == 6 ? VriAttachmentStoreOp_DontCare : VriAttachmentStoreOp_Store;
                    stencil.clearValue.depthStencil = {0, stencilBases[stage] + cell};
                    VriAttachmentsDesc attachments {};
                    attachments.depth      = stage == 3 ? nullptr : &depth;
                    attachments.stencil    = stage == 4 ? nullptr : &stencil;
                    attachments.renderArea = {0, 0, desc.width >> mip, desc.height >> mip};
                    attachments.layerNum   = 1;
                    probe.device.core.CmdBeginRendering(*commands, &attachments);
                    probe.device.core.CmdEndRendering(*commands);
                }
            }
            if (auto submitted = probe.frame->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
            {
                for (uint32_t layer = 0; layer < layers; ++layer)
                {
                    const uint32_t     cell    = mip * layers + layer;
                    const float        depth   = float(stage >= 1 && stage <= 3 ? 4 - cell : 1 + cell) * .25f;
                    const uint32_t     stencil = stencilBases[stage] + cell;
                    VriTextureViewDesc view {(*texture)->handle,
                                             VriTextureViewType_2D,
                                             VriFormat_Unknown,
                                             VriImageAspect_Depth,
                                             mip,
                                             1,
                                             layer,
                                             1};
                    const Words        dimensions {desc.width >> mip, desc.height >> mip, 1, 1};
                    if (stage != 5)
                    {
                        auto depthRead = probe.sample(**texture,
                                                      view,
                                                      7,
                                                      {},
                                                      std::bit_cast<Words>(Pixel {depth, 0, 0, 1}),
                                                      dimensions);
                        if (!depthRead)
                        {
                            return std::unexpected("Depth retention stage " + std::to_string(stage) + ": " +
                                                   depthRead.error());
                        }
                    }
                    view.aspect = VriImageAspect_Stencil;
                    if (stage != 6)
                    {
                        auto stencilRead = probe.sample(**texture, view, 8, {}, {stencil, 0, 0, 1}, dimensions);
                        if (!stencilRead)
                        {
                            return std::unexpected("Stencil retention stage " + std::to_string(stage) + ": " +
                                                   stencilRead.error());
                        }
                    }
                }
            }
            std::printf("STENCIL_OPERATIONS layers=%u stage=%u samples=%u passed\n", layers, stage, probe.samples);
        }
        return {};
    }

    std::expected<void, std::string> verify(Device& device, const ShaderProgram& program)
    {
        Probe probe {device};
        if (auto result = probe.initialize(program); !result)
        {
            return result;
        }
        if (auto result = verifyColorViews(probe); !result)
        {
            return result;
        }
        for (const auto format : {VriFormat_D32_SFLOAT, VriFormat_D32_SFLOAT_S8_UINT, VriFormat_RGBA8_UNORM})
        {
            for (const auto layers : {1u, 2u})
            {
                if (auto result = verifyAttachments(probe, format, layers); !result)
                {
                    return result;
                }
            }
        }
        for (const auto layers : {1u, 2u})
        {
            if (auto result = verifyStencilOperations(probe, layers); !result)
            {
                return result;
            }
        }
        std::printf("VIEW_READBACK samples=%u passed\n", probe.samples);
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-texture-views shader.vshaderc\n", stderr);
        return 1;
    }
    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0)
    {
        return 1;
    }
    std::vector<std::byte> bytes(size_t(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        return 1;
    }
    auto program = ShaderProgram::loadExpected(bytes, argv[1]);
    auto device  = Device::create(true);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    if (auto result = verify(**device, *program); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
