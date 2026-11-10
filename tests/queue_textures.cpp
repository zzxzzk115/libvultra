#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <vri/ext/vri_ext_interop.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace vultra;
    constexpr uint32_t width      = 32;
    constexpr uint32_t height     = 16;
    constexpr uint32_t mipCount   = 3;
    constexpr uint32_t layerCount = 2;
    constexpr uint32_t viewCount  = mipCount * layerCount;
    constexpr uint64_t timeoutNs  = 5'000'000'000;

    struct Parameters
    {
        uint32_t cycle;
        uint32_t iterations;
        uint32_t width;
        uint32_t height;
    };

    struct Subresource
    {
        uint32_t                             mip;
        uint32_t                             layer;
        uint32_t                             width;
        uint32_t                             height;
        uint32_t                             rowBytes;
        uint64_t                             offset;
        std::array<std::vector<uint32_t>, 2> expected;
    };

    struct Bindings
    {
        Device&                                              device;
        std::array<VriPipelineLayout*, 3>                    layouts {};
        VriDescriptorPool*                                   pool    = nullptr;
        VriDescriptorSet*                                    workSet = nullptr;
        std::array<VriDescriptorSet*, viewCount>             resolveSets {};
        std::array<std::array<VriDescriptor*, viewCount>, 2> views {};
        VriDescriptor*                                       scratchView = nullptr;
        std::array<std::unique_ptr<ShaderPipeline>, 3>       pipelines;
        std::array<VriFence*, 3>                             ready {};

        ~Bindings()
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
            if (scratchView)
            {
                device.core.DestroyDescriptor(scratchView);
            }
            for (auto* fence : ready)
            {
                if (fence)
                {
                    device.core.DestroyFence(fence);
                }
            }
            for (auto* layout : layouts)
            {
                if (layout)
                {
                    device.core.DestroyPipelineLayout(layout);
                }
            }
        }
    };

    // Release parked Metal submissions before Frame destruction on an early-return path.
    struct ReleaseStart
    {
        Device&   device;
        VriQueue* transferQueue;
        VriFence* start;
        uint64_t  value = 0;

        std::expected<void, std::string> signal()
        {
            const VriFenceSubmitDesc signal {start, value, VriPipelineStage_AllCommands};
            VriQueueSubmitDesc       submit {};
            submit.signalFences   = &signal;
            submit.signalFenceNum = 1;
            const auto result     = device.core.QueueSubmit(transferQueue, &submit);
            if (result != VriResult_Success)
            {
                return std::unexpected("Release queue start gate: " + std::to_string(result));
            }
            value = 0;
            return {};
        }

        ~ReleaseStart()
        {
            if (value)
            {
                if (auto released = signal(); !released)
                {
                    std::fprintf(stderr, "%s\n", released.error().c_str());
                }
            }
        }
    };

    uint32_t evaluate(uint32_t value, uint32_t iterations)
    {
        for (uint32_t i = 0; i < iterations; ++i)
        {
            value = (value ^ (value >> 13)) * 1664525u + 1013904223u;
        }
        return value;
    }

    std::expected<void, std::string>
    transition(VriCommandBuffer* commands, Texture& texture, const Subresource& region, VriAccessLayoutStage next)
    {
        VriTextureViewDesc view {};
        view.viewType  = VriTextureViewType_MaxEnum;
        view.baseMip   = region.mip;
        view.mipNum    = 1;
        view.baseLayer = region.layer;
        view.layerNum  = 1;
        return texture.transitionExpected(commands, next, view);
    }

    VriBufferTextureCopyDesc copyRegion(const Subresource& region, uint64_t offset = 0)
    {
        VriBufferTextureCopyDesc copy {};
        copy.bufferOffset      = offset + region.offset;
        copy.bufferRowLength   = region.rowBytes / sizeof(uint32_t);
        copy.bufferImageHeight = region.height;
        copy.texture = {region.mip, region.layer, 1, VriImageAspect_Color, 0, 0, 0, region.width, region.height, 1};
        return copy;
    }

    std::expected<void, std::string> createBindings(Bindings&                                      bindings,
                                                    const ShaderProgram&                           program,
                                                    std::span<Subresource>                         regions,
                                                    const std::array<std::unique_ptr<Texture>, 2>& textures,
                                                    Buffer&                                        scratch)
    {
        auto&            core = bindings.device.core;
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute},
            VriDescriptorRangeDesc {2, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
        const VriDescriptorSetDesc work {0, &ranges[2], 1};
        const VriDescriptorSetDesc resolve {0, ranges.data(), uint32_t(ranges.size())};
        for (size_t index = 0; index < bindings.layouts.size(); ++index)
        {
            const auto stages = index == 0 ? VriShaderStageFlags(VriShaderStage_Vertex | VriShaderStage_Fragment) :
                                             VriShaderStageFlags(VriShaderStage_Compute);
            const VriPushConstantDesc push {0, sizeof(Parameters), stages};
            VriPipelineLayoutDesc     desc {};
            desc.pushConstants   = &push;
            desc.pushConstantNum = 1;
            desc.shaderStages    = stages;
            if (index != 0)
            {
                desc.descriptorSets   = index == 1 ? &work : &resolve;
                desc.descriptorSetNum = 1;
            }
            const auto result = core.CreatePipelineLayout(bindings.device.handle, &desc, &bindings.layouts[index]);
            if (result != VriResult_Success)
            {
                return std::unexpected("Create texture queue layout: " + std::to_string(result));
            }
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = 1 + viewCount;
        pool.textureMaxNum        = viewCount;
        pool.storageTextureMaxNum = viewCount;
        pool.storageBufferMaxNum  = 1 + viewCount;
        auto result               = core.CreateDescriptorPool(bindings.device.handle, &pool, &bindings.pool);
        if (result != VriResult_Success)
        {
            return std::unexpected("Create texture queue descriptor pool: " + std::to_string(result));
        }
        const VriBufferViewDesc buffer {scratch.handle,
                                        VriDescriptorType_StorageBuffer,
                                        VriFormat_Unknown,
                                        0,
                                        scratch.desc.size};
        result = core.CreateBufferView(bindings.device.handle, &buffer, &bindings.scratchView);
        if (result != VriResult_Success)
        {
            return std::unexpected("Create independent compute view: " + std::to_string(result));
        }
        result = core.AllocateDescriptorSets(bindings.pool, bindings.layouts[1], 0, &bindings.workSet, 1);
        if (result != VriResult_Success)
        {
            return std::unexpected("Allocate independent compute set: " + std::to_string(result));
        }
        const VriDescriptor*               scratchDescriptor = bindings.scratchView;
        const VriDescriptorRangeUpdateDesc workUpdate {&scratchDescriptor, 1};
        core.UpdateDescriptorRanges(bindings.workSet, 0, 1, &workUpdate);
        for (size_t index = 0; index < regions.size(); ++index)
        {
            const auto& region = regions[index];
            for (size_t texture = 0; texture < textures.size(); ++texture)
            {
                const VriTextureViewDesc view {textures[texture]->handle,
                                               VriTextureViewType_2D,
                                               VriFormat_Unknown,
                                               VriImageAspect_Color,
                                               region.mip,
                                               1,
                                               region.layer,
                                               1};
                auto                     selected = textures[texture]->viewExpected(view);
                if (!selected)
                {
                    return std::unexpected(selected.error());
                }
                bindings.views[texture][index] = *selected;
            }
            result =
                core.AllocateDescriptorSets(bindings.pool, bindings.layouts[2], 0, &bindings.resolveSets[index], 1);
            if (result != VriResult_Success)
            {
                return std::unexpected("Allocate texture consume set: " + std::to_string(result));
            }
            const std::array<const VriDescriptor*, 3> views {bindings.views[0][index],
                                                             bindings.views[1][index],
                                                             bindings.scratchView};
            const std::array                          updates {VriDescriptorRangeUpdateDesc {&views[0], 1},
                                      VriDescriptorRangeUpdateDesc {&views[1], 1},
                                      VriDescriptorRangeUpdateDesc {&views[2], 1}};
            core.UpdateDescriptorRanges(bindings.resolveSets[index], 0, uint32_t(updates.size()), updates.data());
        }
        for (auto& fence : bindings.ready)
        {
            result = core.CreateFence(bindings.device.handle, 0, &fence);
            if (result != VriResult_Success)
            {
                return std::unexpected("Create texture queue dependency fence: " + std::to_string(result));
            }
        }
        const std::array entries {"fragmentMain", "independentMain", "consumeMain"};
        for (size_t index = 0; index < bindings.pipelines.size(); ++index)
        {
            std::vector<ShaderEntry> stages;
            if (index == 0)
            {
                stages = {{"vertexMain", VriShaderStage_Vertex}, {entries[index], VriShaderStage_Fragment}};
            }
            else
            {
                stages = {{entries[index], VriShaderStage_Compute}};
            }
            auto pipeline = ShaderPipeline::create(
                bindings.device,
                program,
                stages,
                [&, index](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriPipeline* pipeline = nullptr;
                    VriResult    created  = VriResult_Failure;
                    if (index == 0)
                    {
                        VriColorAttachmentDesc color {};
                        color.format         = VriFormat_R32_UINT;
                        color.colorWriteMask = VriColorWrite_R;
                        VriGraphicsPipelineDesc desc {};
                        desc.pipelineLayout          = bindings.layouts[0];
                        desc.pipelineCache           = bindings.device.pipelineCache;
                        desc.shaders                 = shaders.data();
                        desc.shaderNum               = uint32_t(shaders.size());
                        desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                        desc.rasterization.cullMode  = VriCullMode_None;
                        desc.rasterization.lineWidth = 1;
                        desc.multisample.sampleNum   = 1;
                        desc.outputMerger.colors     = &color;
                        desc.outputMerger.colorNum   = 1;
                        created = core.CreateGraphicsPipeline(bindings.device.handle, &desc, &pipeline);
                    }
                    else
                    {
                        const VriComputePipelineDesc desc {bindings.layouts[index],
                                                           shaders[0],
                                                           bindings.device.pipelineCache};
                        created = core.CreateComputePipeline(bindings.device.handle, &desc, &pipeline);
                    }
                    if (created != VriResult_Success)
                    {
                        if (pipeline)
                        {
                            core.DestroyPipeline(pipeline);
                        }
                        return std::unexpected("Create texture queue pipeline: " + std::to_string(created));
                    }
                    return pipeline;
                });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            bindings.pipelines[index] = std::move(*pipeline);
        }
        return {};
    }

    std::expected<uint64_t, std::string>
    check(Device& device, Buffer& buffer, std::span<const Subresource> regions, size_t texture)
    {
        const auto* bytes = static_cast<const std::byte*>(device.core.MapBuffer(buffer.handle, 0, buffer.desc.size));
        if (!bytes)
        {
            return std::unexpected("Map texture queue result failed");
        }
        uint64_t    checksum = 1469598103934665603ull;
        std::string error;
        for (const auto& region : regions)
        {
            for (uint32_t y = 0; y < region.height; ++y)
            {
                for (uint32_t x = 0; x < region.width; ++x)
                {
                    uint32_t actual = 0;
                    std::memcpy(&actual,
                                bytes + region.offset + uint64_t(y) * region.rowBytes + x * sizeof(actual),
                                sizeof(actual));
                    const auto expected = region.expected[texture][y * region.width + x];
                    checksum            = (checksum ^ actual) * 1099511628211ull;
                    if (actual != expected && error.empty())
                    {
                        error = "Texture queue mismatch: texture=" + std::to_string(texture) +
                                " mip=" + std::to_string(region.mip) + " layer=" + std::to_string(region.layer) +
                                " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual);
                    }
                }
            }
        }
        device.core.UnmapBuffer(buffer.handle);
        if (!error.empty())
        {
            return std::unexpected(std::move(error));
        }
        return checksum;
    }

    std::expected<void, std::string>
    probe(Device& device, const ShaderProgram& program, uint32_t frames, uint32_t iterations, bool serializeIndependent)
    {
        std::array<Subresource, viewCount> regions {};
        const auto                         alignment =
            std::max(256u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment);
        uint64_t bytes = 0;
        for (uint32_t index = 0; index < viewCount; ++index)
        {
            auto& region    = regions[index];
            region.mip      = index % mipCount;
            region.layer    = index / mipCount;
            region.width    = width >> region.mip;
            region.height   = height >> region.mip;
            region.rowBytes = ((region.width * sizeof(uint32_t) + alignment - 1) / alignment) * alignment;
            region.offset   = bytes;
            bytes += uint64_t(region.rowBytes) * region.height;
            region.expected[0].assign(region.width * region.height, 0x13579bdfu);
            region.expected[1].assign(region.width * region.height, 0x2468ace0u);
        }
        const auto supported = device.core.GetFormatSupport(device.handle, VriFormat_R32_UINT);
        if (!(supported & VriFormatSupport_ColorAttachment) || !(supported & VriFormatSupport_StorageTexture))
        {
            return std::unexpected("Texture queue test requires R32_UINT attachment/storage support");
        }
        VriTextureDesc desc {};
        desc.type           = VriTextureType_2DArray;
        desc.format         = VriFormat_R32_UINT;
        desc.width          = width;
        desc.height         = height;
        desc.depth          = 1;
        desc.mipNum         = mipCount;
        desc.layerNum       = layerCount;
        desc.sampleNum      = 1;
        desc.memoryLocation = VriMemoryLocation_Device;
        desc.usage = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc |
                     VriTextureUsage_TransferDst;
        auto first  = Texture::create(device, desc);
        desc.usage  = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        auto second = Texture::create(device, desc);
        if (!first || !second)
        {
            return std::unexpected("Create texture queue images: " + (!first ? first.error() : second.error()));
        }
        std::array<std::unique_ptr<Texture>, 2> textures {std::move(*first), std::move(*second)};
        auto upload  = Buffer::create(device, {2 * bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto scratch = Buffer::create(device,
                                      {width * height * sizeof(uint32_t),
                                       sizeof(uint32_t),
                                       VriBufferUsage_StorageBuffer,
                                       VriMemoryLocation_Device});
        std::array<std::unique_ptr<Buffer>, 2> readbacks;
        if (!upload || !scratch)
        {
            return std::unexpected("Create texture queue buffers: " + (!upload ? upload.error() : scratch.error()));
        }
        for (auto& readback : readbacks)
        {
            auto created =
                Buffer::create(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!created)
            {
                return std::unexpected(created.error());
            }
            readback = std::move(*created);
        }
        auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*upload)->handle, 0, 2 * bytes));
        if (!mapped)
        {
            return std::unexpected("Map texture initialization failed");
        }
        std::memset(mapped, 0, 2 * bytes);
        for (size_t texture = 0; texture < textures.size(); ++texture)
        {
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    std::memcpy(mapped + texture * bytes + region.offset + uint64_t(y) * region.rowBytes,
                                region.expected[texture].data() + y * region.width,
                                region.width * sizeof(uint32_t));
                }
            }
        }
        device.core.UnmapBuffer((*upload)->handle);
        Bindings bindings {device};
        if (auto created = createBindings(bindings, program, regions, textures, **scratch); !created)
        {
            return created;
        }
        auto initialize  = Frame::create(device, VriQueueType_Transfer);
        auto graphics    = Frame::create(device, VriQueueType_Graphics);
        auto independent = Frame::create(device, VriQueueType_Compute);
        auto consume     = Frame::create(device, VriQueueType_Compute);
        auto transfer    = Frame::create(device, VriQueueType_Transfer);
        if (!initialize || !graphics || !independent || !consume || !transfer)
        {
            return std::unexpected("Create texture queue Frames failed");
        }
        VriInteropInterface interop {};
        auto                result = vriGetInterface(device.handle, VRI_INTERFACE_INTEROP, sizeof(interop), &interop);
        if (result != VriResult_Success || !interop.GetQueueNativeHandle)
        {
            return std::unexpected("Get queue identity diagnostics: " + std::to_string(result));
        }
        std::array<VriQueue*, 3> queues {};
        std::array<void*, 3>     native {};
        for (uint32_t index = 0; index < queues.size(); ++index)
        {
            result = device.core.GetQueue(device.handle, VriQueueType(index), 0, &queues[index]);
            if (result != VriResult_Success)
            {
                return std::unexpected("Get texture queue: " + std::to_string(result));
            }
            native[index] = interop.GetQueueNativeHandle(queues[index]);
            if (!native[index])
            {
                return std::unexpected("Missing native queue identity");
            }
        }
        const bool distinct = native[0] != native[1] && native[0] != native[2] && native[1] != native[2];
        const bool metal    = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_Metal;
        if (metal && !distinct)
        {
            return std::unexpected("Metal texture queue test requires its three independent native queues");
        }
        std::printf("TEXTURE_QUEUES_DEVICE native_metal=%d native_queues_distinct=%d iterations=%u frames=%u "
                    "serialize_independent=%d\n",
                    int(metal),
                    int(distinct),
                    iterations,
                    frames,
                    int(serializeIndependent));
        std::fflush(stdout);
        ReleaseStart release {device, queues[2], bindings.ready[0]};
        auto         commands = (*initialize)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        (*upload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        for (size_t texture = 0; texture < textures.size(); ++texture)
        {
            for (auto& region : regions)
            {
                if (auto transitioned = transition(
                        *commands,
                        *textures[texture],
                        region,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                    !transitioned)
                {
                    return transitioned;
                }
                const auto copy = copyRegion(region, texture * bytes);
                device.core.CmdUploadBufferToTexture(*commands, textures[texture]->handle, (*upload)->handle, &copy);
            }
        }
        if (auto submitted = (*initialize)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        for (uint32_t cycle = 0; cycle < frames; ++cycle)
        {
            const auto       selected = (cycle % layerCount) * mipCount + cycle % mipCount;
            auto&            region   = regions[selected];
            const Parameters parameters {cycle, iterations, region.width, region.height};
            auto             draw         = (*graphics)->beginExpected();
            auto             work         = (*independent)->beginExpected();
            auto             resolve      = (*consume)->beginExpected();
            auto             copyCommands = (*transfer)->beginExpected();
            if (!draw || !work || !resolve || !copyCommands)
            {
                return std::unexpected("Begin texture queue recording failed");
            }
            auto& core = device.core;
            core.CmdBeginDebugGroup(*draw, "QueueTextures.Graphics");
            if (auto transitioned = transition(*draw,
                                               *textures[0],
                                               region,
                                               {VriAccess_ColorAttachmentWrite,
                                                VriLayout_ColorAttachment,
                                                VriPipelineStage_ColorAttachmentOutput});
                !transitioned)
            {
                return transitioned;
            }
            VriAttachmentDesc color {};
            color.view    = bindings.views[0][selected];
            color.loadOp  = VriAttachmentLoadOp_DontCare;
            color.storeOp = VriAttachmentStoreOp_Store;
            VriAttachmentsDesc attachments {};
            attachments.colors     = &color;
            attachments.colorNum   = 1;
            attachments.layerNum   = 1;
            attachments.renderArea = {0, 0, region.width, region.height};
            core.CmdBeginRendering(*draw, &attachments);
            const VriViewport viewport {0, 0, float(region.width), float(region.height), 0, 1};
            core.CmdSetViewports(*draw, &viewport, 1);
            core.CmdSetScissors(*draw, &attachments.renderArea, 1);
            core.CmdSetPipelineLayout(*draw, bindings.layouts[0]);
            core.CmdSetPipeline(*draw, bindings.pipelines[0]->handle());
            core.CmdSetConstants(*draw, 0, &parameters, sizeof(parameters));
            const VriDrawDesc triangle {3, 1, 0, 0};
            core.CmdDraw(*draw, &triangle);
            core.CmdEndRendering(*draw);
            core.CmdEndDebugGroup(*draw);

            core.CmdBeginDebugGroup(*work, "QueueTextures.IndependentCompute");
            (*scratch)->transition(*work, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            core.CmdSetPipelineLayout(*work, bindings.layouts[1]);
            core.CmdSetPipeline(*work, bindings.pipelines[1]->handle());
            // This set binds only scratch: binding an unused graphics texture could add a false native hazard.
            core.CmdSetDescriptorSet(*work, 0, bindings.workSet);
            core.CmdSetConstants(*work, 0, &parameters, sizeof(parameters));
            const VriDispatchDesc groups {(region.width + 7) / 8, (region.height + 7) / 8, 1};
            core.CmdDispatch(*work, &groups);
            core.CmdEndDebugGroup(*work);

            core.CmdBeginDebugGroup(*resolve, "QueueTextures.Consume");
            if (auto transitioned = transition(
                    *resolve,
                    *textures[0],
                    region,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
                !transitioned)
            {
                return transitioned;
            }
            if (auto transitioned = transition(
                    *resolve,
                    *textures[1],
                    region,
                    {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_ComputeShader});
                !transitioned)
            {
                return transitioned;
            }
            (*scratch)->transition(*resolve, {VriAccess_ShaderResourceStorageRead, VriPipelineStage_ComputeShader});
            core.CmdSetPipelineLayout(*resolve, bindings.layouts[2]);
            core.CmdSetPipeline(*resolve, bindings.pipelines[2]->handle());
            core.CmdSetDescriptorSet(*resolve, 0, bindings.resolveSets[selected]);
            core.CmdSetConstants(*resolve, 0, &parameters, sizeof(parameters));
            core.CmdDispatch(*resolve, &groups);
            core.CmdEndDebugGroup(*resolve);

            core.CmdBeginDebugGroup(*copyCommands, "QueueTextures.ReadbackAllMipsAndLayers");
            for (size_t texture = 0; texture < textures.size(); ++texture)
            {
                readbacks[texture]->transition(*copyCommands,
                                               {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                for (auto& subresource : regions)
                {
                    if (auto transitioned =
                            transition(*copyCommands,
                                       *textures[texture],
                                       subresource,
                                       {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
                        !transitioned)
                    {
                        return transitioned;
                    }
                    const auto copy = copyRegion(subresource);
                    core.CmdReadbackTextureToBuffer(*copyCommands,
                                                    readbacks[texture]->handle,
                                                    textures[texture]->handle,
                                                    &copy);
                }
            }
            core.CmdEndDebugGroup(*copyCommands);
            const VriFenceSubmitDesc start {bindings.ready[0], cycle + 1, VriPipelineStage_AllCommands};
            const VriFenceSubmitDesc rendered {bindings.ready[1], cycle + 1, VriPipelineStage_AllCommands};
            const VriFenceSubmitDesc consumed {bindings.ready[2], cycle + 1, VriPipelineStage_AllCommands};
            const std::span<const VriFenceSubmitDesc> startGate =
                metal ? std::span(&start, 1) : std::span<const VriFenceSubmitDesc> {};
            if (metal)
            {
                release.value = cycle + 1;
            }
            if (auto submitted = (*graphics)->submitExpected(startGate, std::span(&rendered, 1)); !submitted)
            {
                return submitted;
            }
            // The capture control changes only this GPU dependency, retaining identical work and readback.
            const auto independentWait = serializeIndependent ? std::span(&rendered, 1) : startGate;
            if (auto submitted = (*independent)->submitExpected(independentWait); !submitted)
            {
                return submitted;
            }
            if (metal)
            {
                if ((*graphics)->waitExpected(0) || (*independent)->waitExpected(0))
                {
                    return std::unexpected("Common GPU start gate did not hold both submissions");
                }
                if (auto started = release.signal(); !started)
                {
                    return started;
                }
            }
            if (auto submitted = (*consume)->submitExpected(std::span(&rendered, 1), std::span(&consumed, 1));
                !submitted)
            {
                return submitted;
            }
            if (auto submitted = (*transfer)->submitExpected(std::span(&consumed, 1)); !submitted)
            {
                return submitted;
            }
            if (!(*transfer)->waitExpected(timeoutNs) || !(*consume)->waitExpected(timeoutNs) ||
                !(*independent)->waitExpected(timeoutNs) || !(*graphics)->waitExpected(timeoutNs))
            {
                return std::unexpected("Texture queue chain did not complete within its deadline");
            }
            for (uint32_t index = 0; index < region.width * region.height; ++index)
            {
                const auto raster         = evaluate(0x10203u + cycle * 4099u + index, iterations);
                const auto computed       = evaluate(0x60107u + cycle * 6151u + index, iterations);
                region.expected[0][index] = raster;
                region.expected[1][index] = raster ^ computed;
            }
            auto firstChecksum  = check(device, *readbacks[0], regions, 0);
            auto secondChecksum = check(device, *readbacks[1], regions, 1);
            if (!firstChecksum || !secondChecksum)
            {
                return std::unexpected(!firstChecksum ? firstChecksum.error() : secondChecksum.error());
            }
            std::printf("TEXTURE_QUEUES cycle=%u mip=%u layer=%u all_subresources_exact=1 checksum=%llu,%llu\n",
                        cycle,
                        region.mip,
                        region.layer,
                        static_cast<unsigned long long>(*firstChecksum),
                        static_cast<unsigned long long>(*secondChecksum));
            std::fflush(stdout);
        }
        return {};
    }

    std::expected<uint32_t, std::string> number(std::string_view value, uint32_t maximum)
    {
        uint32_t   parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc {} || result.ptr != value.data() + value.size() || parsed > maximum)
        {
            return std::unexpected("Invalid finite queue workload parameter");
        }
        return parsed;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fputs("Usage: test-queue-textures probe.vshaderc [--iterations N] [--frames N] "
                   "[--serialize-independent]\n",
                   stderr);
        return 2;
    }
    uint32_t frames               = 6;
    uint32_t iterations           = 4;
    bool     serializeIndependent = false;
    for (int index = 2; index < argc; ++index)
    {
        const std::string_view flag = argv[index];
        if (flag == "--serialize-independent")
        {
            serializeIndependent = true;
            continue;
        }
        if (index + 1 >= argc || (flag != "--iterations" && flag != "--frames"))
        {
            std::fputs("Invalid queue texture arguments\n", stderr);
            return 2;
        }
        auto parsed = number(argv[++index], flag == "--frames" ? 128u : 1'048'576u);
        if (!parsed || (flag == "--frames" && *parsed == 0))
        {
            std::fputs("Queue texture arguments exceed the finite test range\n", stderr);
            return 2;
        }
        (flag == "--frames" ? frames : iterations) = *parsed;
    }
    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0 || stream.tellg() > 64 * 1024 * 1024)
    {
        std::fputs("Cannot read texture queue shader\n", stderr);
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        std::fputs("Cannot read complete texture queue shader\n", stderr);
        return 1;
    }
    auto program = ShaderProgram::loadExpected(bytes, argv[1]);
    auto device  = Device::create(true);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    if (auto result = probe(**device, *program, frames, iterations, serializeIndependent); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Graphics/compute texture handoff and independent work readback passed");
    return 0;
}
