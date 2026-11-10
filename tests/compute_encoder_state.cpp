#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <vri/ext/vri_ext_query.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace vultra;

    struct Handles
    {
        Device&                                        device;
        VriPipelineLayout*                             layout = nullptr;
        VriDescriptorPool*                             pool   = nullptr;
        std::array<VriDescriptorSet*, 4>               sets {};
        std::array<std::unique_ptr<ShaderPipeline>, 2> pipelines;
        std::vector<VriDescriptor*>                    views;
        VriQueryInterface                              query {};
        VriQueryPool*                                  timestamps = nullptr;

        ~Handles()
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
            for (auto* view : views)
            {
                device.core.DestroyDescriptor(view);
            }
            if (timestamps)
            {
                query.DestroyQueryPool(timestamps);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    std::expected<std::unique_ptr<Texture>, std::string> solidTexture(Device& device, float value)
    {
        TextureAssetData data;
        data.format = TextureFormat::eRgba32Sfloat;
        TextureSubresource level;
        level.size = {1, 1};
        const std::array<float, 4> pixel {value, 0, 0, 1};
        level.bytes.resize(sizeof(pixel));
        std::memcpy(level.bytes.data(), pixel.data(), sizeof(pixel));
        data.subresources.push_back(std::move(level));
        return uploadTextureAssetExpected(device, data);
    }

    std::expected<VriDescriptor*, std::string> bufferView(Handles& handles, Buffer& buffer, VriDescriptorType type)
    {
        VriDescriptor*          view = nullptr;
        const VriBufferViewDesc desc {buffer.handle, type, VriFormat_Unknown, 0, buffer.desc.size};
        const auto              status = handles.device.core.CreateBufferView(handles.device.handle, &desc, &view);
        if (view)
        {
            handles.views.push_back(view);
        }
        if (status != VriResult_Success || !view)
        {
            return std::unexpected("Create compute-state buffer view: " + std::to_string(status));
        }
        return view;
    }

    std::expected<void, std::string> createBindings(Handles&                                       handles,
                                                    Buffer&                                        first,
                                                    Buffer&                                        second,
                                                    Buffer&                                        output,
                                                    const std::array<std::unique_ptr<Texture>, 6>& textures,
                                                    const ShaderProgram&                           program)
    {
        auto&            device = handles.device;
        const std::array direct {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {2, 1, VriDescriptorType_Sampler, VriShaderStage_Compute},
            VriDescriptorRangeDesc {3, 1, VriDescriptorType_Texture, VriShaderStage_Compute}};
        const VriDescriptorRangeDesc array {0,
                                            2,
                                            VriDescriptorType_Texture,
                                            VriShaderStage_Compute,
                                            VriDescriptorRange_Array};
        const std::array             sets {VriDescriptorSetDesc {0, direct.data(), uint32_t(direct.size())},
                               VriDescriptorSetDesc {1, &array, 1}};
        const VriPushConstantDesc    push {0, 16, VriShaderStage_Compute};
        const VriPipelineLayoutDesc  layout {sets.data(), uint32_t(sets.size()), &push, 1, VriShaderStage_Compute};
        auto                         status = device.core.CreatePipelineLayout(device.handle, &layout, &handles.layout);
        if (status != VriResult_Success || !handles.layout)
        {
            return std::unexpected("Create compute-state layout: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = 4;
        pool.structuredBufferMaxNum = 2;
        pool.storageBufferMaxNum    = 2;
        pool.samplerMaxNum          = 2;
        pool.textureMaxNum          = 6;
        status                      = device.core.CreateDescriptorPool(device.handle, &pool, &handles.pool);
        if (status != VriResult_Success || !handles.pool)
        {
            return std::unexpected("Create compute-state pool: " + std::to_string(status));
        }
        for (uint32_t index = 0; index < handles.sets.size(); ++index)
        {
            status =
                device.core.AllocateDescriptorSets(handles.pool, handles.layout, index / 2, &handles.sets[index], 1);
            if (status != VriResult_Success || !handles.sets[index])
            {
                return std::unexpected("Allocate compute-state set: " + std::to_string(status));
            }
        }
        VriSamplerDesc sampler {};
        sampler.minFilter          = VriFilter_Nearest;
        sampler.magFilter          = VriFilter_Nearest;
        sampler.mipmapMode         = VriMipmapMode_Nearest;
        sampler.addressModeU       = VriAddressMode_ClampToEdge;
        sampler.addressModeV       = VriAddressMode_ClampToEdge;
        sampler.addressModeW       = VriAddressMode_ClampToEdge;
        VriDescriptor* samplerView = nullptr;
        status                     = device.core.CreateSampler(device.handle, &sampler, &samplerView);
        if (samplerView)
        {
            handles.views.push_back(samplerView);
        }
        if (status != VriResult_Success || !samplerView)
        {
            return std::unexpected("Create compute-state sampler: " + std::to_string(status));
        }
        auto firstView  = bufferView(handles, first, VriDescriptorType_StructuredBuffer);
        auto secondView = bufferView(handles, second, VriDescriptorType_StructuredBuffer);
        auto outputView = bufferView(handles, output, VriDescriptorType_StorageBuffer);
        if (!firstView)
        {
            return std::unexpected(firstView.error());
        }
        if (!secondView)
        {
            return std::unexpected(secondView.error());
        }
        if (!outputView)
        {
            return std::unexpected(outputView.error());
        }
        std::array<const VriDescriptor*, 6> textureViews {};
        for (size_t index = 0; index < textures.size(); ++index)
        {
            auto view = textures[index]->viewExpected();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            textureViews[index] = *view;
        }
        for (uint32_t index = 0; index < 2; ++index)
        {
            const std::array<const VriDescriptor*, 4> views {index == 0 ? *firstView : *secondView,
                                                             *outputView,
                                                             samplerView,
                                                             textureViews[index * 3 + 2]};
            const std::array updates {VriDescriptorRangeUpdateDesc {&views[0], 1},
                                      VriDescriptorRangeUpdateDesc {&views[1], 1},
                                      VriDescriptorRangeUpdateDesc {&views[2], 1},
                                      VriDescriptorRangeUpdateDesc {&views[3], 1}};
            device.core.UpdateDescriptorRanges(handles.sets[index], 0, uint32_t(updates.size()), updates.data());
            const VriDescriptorRangeUpdateDesc texturesUpdate {textureViews.data() + index * 3, 2};
            device.core.UpdateDescriptorRanges(handles.sets[index + 2], 0, 1, &texturesUpdate);
        }
        const std::array names {"computeMain", "alternateMain"};
        for (size_t index = 0; index < names.size(); ++index)
        {
            auto pipeline = ShaderPipeline::create(
                device,
                program,
                {{names[index], VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriPipeline*                 result = nullptr;
                    const VriComputePipelineDesc desc {handles.layout, shaders[0], device.pipelineCache};
                    const auto created = device.core.CreateComputePipeline(device.handle, &desc, &result);
                    if (created != VriResult_Success || !result)
                    {
                        if (result)
                        {
                            device.core.DestroyPipeline(result);
                        }
                        return std::unexpected("Create compute-state pipeline: " + std::to_string(created));
                    }
                    return result;
                });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            handles.pipelines[index] = std::move(*pipeline);
        }
        status = vriGetInterface(device.handle, VRI_INTERFACE_QUERY, sizeof(handles.query), &handles.query);
        if (status != VriResult_Success)
        {
            return std::unexpected("Get compute-state query interface: " + std::to_string(status));
        }
        const VriQueryPoolDesc queries {VriQueryType_Timestamp, 5};
        status = handles.query.CreateQueryPool(device.handle, &queries, &handles.timestamps);
        if (status != VriResult_Success || !handles.timestamps)
        {
            return std::unexpected("Create compute-state timestamps: " + std::to_string(status));
        }
        return {};
    }

    std::expected<void, std::string> probe(Device& device, Frame& frame, const ShaderProgram& program, uint32_t cycle)
    {
        constexpr VriAccessStage      read {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader};
        constexpr VriAccessStage      write {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader};
        const std::array<uint32_t, 2> firstData {11, 17};
        const std::array<uint32_t, 2> secondData {101, 107};
        const std::array<uint32_t, 6> zeros {};
        const VriDispatchDesc         dispatch {1, 1, 1};
        auto                          first = uploadBufferExpected(device,
                                          std::as_bytes(std::span(firstData)),
                                          VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                          read,
                                          4);
        auto                          second =
            uploadBufferExpected(device, std::as_bytes(std::span(secondData)), VriBufferUsage_StorageBuffer, read, 4);
        auto output   = uploadBufferExpected(device,
                                           std::as_bytes(std::span(zeros)),
                                           VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                           write,
                                           4);
        auto indirect = uploadBufferExpected(device,
                                             std::as_bytes(std::span(&dispatch, 1)),
                                             VriBufferUsage_IndirectBuffer,
                                             {VriAccess_IndirectBufferRead, VriPipelineStage_DrawIndirect});
        auto readback =
            Buffer::create(device, {sizeof(zeros), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto scratch =
            Buffer::create(device, {sizeof(firstData), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto ticks =
            Buffer::create(device,
                           {5 * sizeof(uint64_t), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!first)
        {
            return std::unexpected("Create first compute-state input: " + first.error());
        }
        if (!second)
        {
            return std::unexpected("Create second compute-state input: " + second.error());
        }
        if (!output)
        {
            return std::unexpected("Create compute-state output: " + output.error());
        }
        if (!indirect)
        {
            return std::unexpected("Create compute-state indirect arguments: " + indirect.error());
        }
        if (!readback)
        {
            return std::unexpected("Create compute-state output readback: " + readback.error());
        }
        if (!scratch)
        {
            return std::unexpected("Create compute-state copy readback: " + scratch.error());
        }
        if (!ticks)
        {
            return std::unexpected("Create compute-state timestamp readback: " + ticks.error());
        }
        std::unique_ptr<Texture> clearTarget;
        if (device.core.GetDeviceDesc(device.handle)->hasClearStorageTexture)
        {
            auto desc    = colorTexture({3, 2}, VriFormat_RGBA32_SFLOAT);
            desc.usage   = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferDst;
            auto texture = Texture::create(device, desc);
            if (!texture)
            {
                return std::unexpected("Create compute-state clear boundary: " + texture.error());
            }
            clearTarget = std::move(*texture);
        }
        std::array<std::unique_ptr<Texture>, 6> textures;
        const std::array                        values {2.f, 8.f, 4.f, 32.f, 64.f, 16.f};
        for (size_t index = 0; index < values.size(); ++index)
        {
            auto texture = solidTexture(device, values[index]);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            textures[index] = std::move(*texture);
        }
        Handles handles {device};
        if (auto created = createBindings(handles, **first, **second, **output, textures, program); !created)
        {
            return created;
        }
        auto commands = frame.beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        auto* cmd = *commands;
        for (auto& texture : textures)
        {
            texture->transition(
                cmd,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
        }
        handles.query.CmdResetQueries(cmd, handles.timestamps, 0, 5);
        const auto parameters = [&](std::array<uint32_t, 4> data)
        {
            device.core.CmdSetConstants(cmd, 0, data.data(), sizeof(data));
        };
        const auto execute = [&]
        {
            device.core.CmdDispatch(cmd, &dispatch);
            (*output)->transition(cmd, write);
            if (clearTarget)
            {
                clearTarget->transition(
                    cmd,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                const VriClearColor value {{9, 8, 7, 6}};
                device.core.CmdClearStorageTexture(cmd, clearTarget->handle, &value);
                // Repeat the idempotent write without rebinding: the internal clear must preserve all user state.
                device.core.CmdDispatch(cmd, &dispatch);
                (*output)->transition(cmd, write);
            }
        };
        const auto copy = [&]
        {
            (*first)->transition(cmd, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*scratch)->transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc region {0, 0, sizeof(firstData)};
            device.core.CmdCopyBuffer(cmd, (*scratch)->handle, (*first)->handle, &region);
            (*first)->transition(cmd, read);
        };
        device.core.CmdSetPipelineLayout(cmd, handles.layout);
        device.core.CmdSetPipeline(cmd, handles.pipelines[0]->handle());
        device.core.CmdSetDescriptorSet(cmd, 0, handles.sets[0]);
        device.core.CmdSetDescriptorSet(cmd, 1, handles.sets[2]);
        parameters({0, 0, 0, 3});
        handles.query.CmdWriteTimestamp(cmd, handles.timestamps, 0);
        execute();
        parameters({1, 1, 1, 2});
        copy();
        execute();
        device.core.CmdSetPipeline(cmd, handles.pipelines[1]->handle());
        parameters({2, 0, 0, 3});
        handles.query.CmdWriteTimestamp(cmd, handles.timestamps, 1);
        execute();
        parameters({3, 0, 0, 1});
        handles.query.CmdWriteTimestamp(cmd, handles.timestamps, 2);
        device.core.CmdSetDescriptorSet(cmd, 0, handles.sets[1]);
        execute();
        device.core.CmdSetDescriptorSet(cmd, 1, handles.sets[3]);
        parameters({4, 1, 1, 1});
        copy();
        execute();
        parameters({5, 0, 0, 2});
        handles.query.CmdWriteTimestamp(cmd, handles.timestamps, 3);
        device.core.CmdDispatchIndirect(cmd, (*indirect)->handle, 0);
        handles.query.CmdWriteTimestamp(cmd, handles.timestamps, 4);
        (*output)->transition(cmd, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        (*readback)->transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc region {0, 0, sizeof(zeros)};
        device.core.CmdCopyBuffer(cmd, (*readback)->handle, (*output)->handle, &region);
        (*ticks)->transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        handles.query.CmdCopyQueries(cmd, handles.timestamps, 0, 5, (*ticks)->handle, 0);
        if (auto submitted = frame.submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        const auto* mapped = device.core.MapBuffer((*readback)->handle, 0, sizeof(zeros));
        if (!mapped)
        {
            return std::unexpected("Map compute-state output failed");
        }
        std::array<uint32_t, 6> actual {};
        std::memcpy(actual.data(), mapped, sizeof(actual));
        device.core.UnmapBuffer((*readback)->handle);
        constexpr std::array<uint32_t, 6> expected {39, 46, 139, 219, 287, 350};
        for (size_t index = 0; index < expected.size(); ++index)
        {
            if (actual[index] != expected[index])
            {
                return std::unexpected("Compute-state mismatch at dispatch " + std::to_string(index) + ": expected " +
                                       std::to_string(expected[index]) + ", got " + std::to_string(actual[index]));
            }
        }
        mapped = device.core.MapBuffer((*ticks)->handle, 0, 5 * sizeof(uint64_t));
        if (!mapped)
        {
            return std::unexpected("Map compute-state timestamp diagnostics failed");
        }
        std::array<uint64_t, 5> timestamps {};
        std::memcpy(timestamps.data(), mapped, sizeof(timestamps));
        device.core.UnmapBuffer((*ticks)->handle);
        std::printf("COMPUTE_STATE cycle=%u values=39,46,139,219,287,350 TIMESTAMP_DIAGNOSTIC ordered=%d ticks=",
                    cycle,
                    int(std::is_sorted(timestamps.begin(), timestamps.end())));
        for (auto timestamp : timestamps)
        {
            std::printf("%llu,", static_cast<unsigned long long>(timestamp));
        }
        std::puts("");
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: test-compute-encoder-state probe.vshaderc\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 64 * 1024 * 1024)
    {
        std::fprintf(stderr, "Cannot read compute-state shader\n");
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        std::fprintf(stderr, "Cannot read complete compute-state shader\n");
        return 1;
    }
    auto program = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    auto device  = vultra::Device::create(true, nullptr, VriFeature_Bindless);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    const auto* caps = (*device)->core.GetDeviceDesc((*device)->handle);
    std::printf("COMPUTE_DEVICE native_metal=%d timestamps=%d enabled_bindless=%d clear_texture=%d\n",
                int(caps->graphicsAPI == VriGraphicsAPI_Metal),
                int(caps->hasTimestampQueries),
                int(caps->hasBindless),
                int(caps->hasClearStorageTexture));
    std::fflush(stdout);
    auto frame = vultra::Frame::create(**device);
    if (!frame)
    {
        std::fprintf(stderr, "%s\n", frame.error().c_str());
        return 1;
    }
    for (uint32_t cycle = 0; cycle < 3; ++cycle)
    {
        if (auto checked = probe(**device, **frame, *program, cycle); !checked)
        {
            std::fprintf(stderr, "%s\n", checked.error().c_str());
            return 1;
        }
    }
    std::puts("Compute binding preservation passed for timestamps, copies, descriptor edits, constants, indirect "
              "dispatch and frame reuse");
    return 0;
}
