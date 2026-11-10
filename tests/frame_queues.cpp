#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

namespace
{
    using namespace vultra;
    constexpr uint64_t timeoutNs = 5'000'000'000;
    constexpr uint64_t dataBytes = 64 * sizeof(uint32_t);

    struct Bindings
    {
        Device&                                        device;
        VriPipelineLayout*                             layout = nullptr;
        VriDescriptorPool*                             pool   = nullptr;
        VriDescriptorSet*                              set    = nullptr;
        std::array<VriDescriptor*, 2>                  views {};
        std::array<VriFence*, 2>                       ready {};
        std::array<std::unique_ptr<ShaderPipeline>, 2> pipelines;

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
            for (auto* view : views)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
            for (auto* fence : ready)
            {
                if (fence)
                {
                    device.core.DestroyFence(fence);
                }
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    // Release a consumer-first Metal wait on every error path before Frame destructors wait.
    struct ReleaseWait
    {
        Device&   device;
        VriQueue* queue;
        VriFence* fence;
        uint64_t  value = 0;

        ~ReleaseWait()
        {
            if (value)
            {
                const VriFenceSubmitDesc signal {fence, value, VriPipelineStage_AllCommands};
                VriQueueSubmitDesc       submit {};
                submit.signalFences   = &signal;
                submit.signalFenceNum = 1;
                if (device.core.QueueSubmit(queue, &submit) != VriResult_Success)
                {
                    std::fputs("Cannot release the pending queue test after failure\n", stderr);
                }
            }
        }
    };

    std::expected<void, std::string>
    createBindings(Bindings& bindings, Buffer& input, Buffer& output, const ShaderProgram& program)
    {
        auto&            core = bindings.device.core;
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPushConstantDesc   constant {0, sizeof(uint32_t), VriShaderStage_Compute};
        const VriPipelineLayoutDesc layout {&set, 1, &constant, 1, VriShaderStage_Compute};
        auto status = core.CreatePipelineLayout(bindings.device.handle, &layout, &bindings.layout);
        if (status != VriResult_Success)
        {
            return std::unexpected("Create queue probe layout: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = 1;
        pool.storageBufferMaxNum = 2;
        status                   = core.CreateDescriptorPool(bindings.device.handle, &pool, &bindings.pool);
        if (status != VriResult_Success)
        {
            return std::unexpected("Create queue probe descriptor pool: " + std::to_string(status));
        }
        status = core.AllocateDescriptorSets(bindings.pool, bindings.layout, 0, &bindings.set, 1);
        if (status != VriResult_Success)
        {
            return std::unexpected("Allocate queue probe descriptors: " + std::to_string(status));
        }
        const std::array buffers {input.handle, output.handle};
        for (size_t index = 0; index < buffers.size(); ++index)
        {
            const VriBufferViewDesc view {buffers[index],
                                          VriDescriptorType_StorageBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          dataBytes};
            status = core.CreateBufferView(bindings.device.handle, &view, &bindings.views[index]);
            if (status != VriResult_Success)
            {
                return std::unexpected("Create queue probe view: " + std::to_string(status));
            }
            status = core.CreateFence(bindings.device.handle, 0, &bindings.ready[index]);
            if (status != VriResult_Success)
            {
                return std::unexpected("Create queue probe dependency fence: " + std::to_string(status));
            }
        }
        const std::array<const VriDescriptor*, 2> views {bindings.views[0], bindings.views[1]};
        const std::array                          updates {VriDescriptorRangeUpdateDesc {&views[0], 1},
                                  VriDescriptorRangeUpdateDesc {&views[1], 1}};
        core.UpdateDescriptorRanges(bindings.set, 0, uint32_t(updates.size()), updates.data());
        const std::array entries {"producerMain", "consumerMain"};
        for (size_t index = 0; index < entries.size(); ++index)
        {
            auto pipeline = ShaderPipeline::create(
                bindings.device,
                program,
                {{entries[index], VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriPipeline*                 result = nullptr;
                    const VriComputePipelineDesc desc {bindings.layout, shaders[0], bindings.device.pipelineCache};
                    const auto created = core.CreateComputePipeline(bindings.device.handle, &desc, &result);
                    if (created != VriResult_Success)
                    {
                        if (result)
                        {
                            core.DestroyPipeline(result);
                        }
                        return std::unexpected("Create queue probe pipeline: " + std::to_string(created));
                    }
                    return result;
                });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            bindings.pipelines[index] = std::move(*pipeline);
        }
        return {};
    }

    void dispatch(Bindings& bindings, VriCommandBuffer* commands, size_t pipeline, uint32_t cycle)
    {
        auto& core = bindings.device.core;
        core.CmdSetPipelineLayout(commands, bindings.layout);
        core.CmdSetPipeline(commands, bindings.pipelines[pipeline]->handle());
        core.CmdSetDescriptorSet(commands, 0, bindings.set);
        core.CmdSetConstants(commands, 0, &cycle, sizeof(cycle));
        const VriDispatchDesc groups {1, 1, 1};
        core.CmdDispatch(commands, &groups);
    }

    std::expected<void, std::string> checkReadback(Device& device, Buffer& readback, uint32_t cycle, bool shaded)
    {
        const auto* mapped = device.core.MapBuffer(readback.handle, 0, dataBytes);
        if (!mapped)
        {
            return std::unexpected("Map queue probe readback failed");
        }
        std::array<uint32_t, 64> values {};
        std::memcpy(values.data(), mapped, dataBytes);
        device.core.UnmapBuffer(readback.handle);
        for (uint32_t index = 0; index < values.size(); ++index)
        {
            const auto source   = index + 11 + cycle * 64;
            const auto expected = shaded ? source * 3 + 7 : source;
            if (values[index] != expected)
            {
                return std::unexpected("Queue readback mismatch at " + std::to_string(index) + ": expected " +
                                       std::to_string(expected) + ", got " + std::to_string(values[index]));
            }
        }
        return {};
    }

    std::expected<void, std::string> unusedFrameWhileWaiting(Device& device)
    {
        const auto      core       = device.core;
        static unsigned idleCalls  = 0;
        idleCalls                  = 0;
        device.core.DeviceWaitIdle = [](VriDevice*)
        {
            ++idleCalls;
            return VriResult_Success;
        };
        {
            auto unused = Frame::create(device);
            if (!unused)
            {
                device.core = core;
                return std::unexpected(unused.error());
            }
        }
        device.core = core;
        if (idleCalls)
        {
            return std::unexpected("Unused Frame cleanup waited on an unrelated pending queue");
        }
        return {};
    }

    std::expected<void, std::string> probe(Device& device, const ShaderProgram& program)
    {
        auto input = Buffer::create(
            device,
            {dataBytes, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        auto output = Buffer::create(
            device,
            {dataBytes, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        auto readback =
            Buffer::create(device, {dataBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!input || !output || !readback)
        {
            return std::unexpected("Create queue probe buffers failed");
        }
        Bindings bindings {device};
        if (auto created = createBindings(bindings, **input, **output, program); !created)
        {
            return created;
        }
        auto      producer    = Frame::create(device, VriQueueType_Graphics);
        auto      consumer    = Frame::create(device, VriQueueType_Compute);
        auto      transfer    = Frame::create(device, VriQueueType_Transfer);
        VriQueue* rescueQueue = nullptr;
        if (!producer || !consumer || !transfer ||
            device.core.GetQueue(device.handle, VriQueueType_Transfer, 0, &rescueQueue) != VriResult_Success)
        {
            return std::unexpected("Create queue probe Frames failed");
        }
        if ((*producer)->queueType() != VriQueueType_Graphics || (*consumer)->queueType() != VriQueueType_Compute ||
            (*transfer)->queueType() != VriQueueType_Transfer)
        {
            return std::unexpected("Frame did not retain its queue selection");
        }
        ReleaseWait release {device, rescueQueue, bindings.ready[0]};
        const auto  nativeMetal = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_Metal;
        const VriBufferCopyDesc copy {0, 0, dataBytes};
        for (uint32_t cycle = 0; cycle < 4; ++cycle)
        {
            auto graphics     = (*producer)->beginExpected();
            auto compute      = (*consumer)->beginExpected();
            auto copyCommands = (*transfer)->beginExpected();
            if (!graphics || !compute || !copyCommands)
            {
                return std::unexpected("Begin queue probe recording failed");
            }
            (*input)->transition(*graphics, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            dispatch(bindings, *graphics, 0, cycle);
            (*input)->transition(*compute, {VriAccess_ShaderResourceStorageRead, VriPipelineStage_ComputeShader});
            (*output)->transition(*compute, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            dispatch(bindings, *compute, 1, cycle);
            (*output)->transition(*copyCommands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*readback)->transition(*copyCommands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            device.core.CmdCopyBuffer(*copyCommands, (*readback)->handle, (*output)->handle, &copy);
            const VriFenceSubmitDesc graphicsReady {bindings.ready[0], cycle + 1, VriPipelineStage_AllCommands};
            const VriFenceSubmitDesc computeReady {bindings.ready[1], cycle + 1, VriPipelineStage_AllCommands};
            const std::span<const VriFenceSubmitDesc> graphicsFence(&graphicsReady, 1);
            const std::span<const VriFenceSubmitDesc> computeFence(&computeReady, 1);
            if (nativeMetal)
            {
                // The pinned Metal backend owns independent native queues. Vulkan queue types may alias.
                release.value = cycle + 1;
                if (auto submitted = (*consumer)->submitExpected(graphicsFence, computeFence); !submitted)
                {
                    return submitted;
                }
                if ((*consumer)->waitExpected(0) || (*consumer)->beginExpected() || (*consumer)->discardExpected())
                {
                    return std::unexpected("Unsignaled GPU dependency did not keep its consumer pending");
                }
                if (auto checked = unusedFrameWhileWaiting(device); !checked)
                {
                    return checked;
                }
            }
            if (auto submitted = (*producer)->submitExpected({}, graphicsFence); !submitted)
            {
                return submitted;
            }
            release.value = 0;
            if (!nativeMetal)
            {
                if (auto submitted = (*consumer)->submitExpected(graphicsFence, computeFence); !submitted)
                {
                    return submitted;
                }
            }
            if (auto submitted = (*transfer)->submitExpected(computeFence); !submitted)
            {
                return submitted;
            }
            // All three queues are submitted before the first blocking host wait.
            if (!(*transfer)->waitExpected(timeoutNs) || !(*consumer)->waitExpected(timeoutNs) ||
                !(*producer)->waitExpected(timeoutNs))
            {
                return std::unexpected("Queue probe completion timed out");
            }
            if (auto checked = checkReadback(device, **readback, cycle, true); !checked)
            {
                return checked;
            }
            auto oldState       = (*input)->captureState();
            graphics            = (*producer)->beginExpected();
            if (!graphics)
            {
                return std::unexpected(graphics.error());
            }
            (*input)->transition(*graphics, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            dispatch(bindings, *graphics, 0, 999);
            if (auto discarded = (*producer)->discardExpected(); !discarded)
            {
                return discarded;
            }
            if (auto restored = (*input)->restoreUnsubmittedExpected(std::move(oldState)); !restored)
            {
                return restored;
            }
            copyCommands    = (*transfer)->beginExpected();
            if (!copyCommands)
            {
                return std::unexpected(copyCommands.error());
            }
            (*input)->transition(*copyCommands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*readback)->transition(*copyCommands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            device.core.CmdCopyBuffer(*copyCommands, (*readback)->handle, (*input)->handle, &copy);
            if (!(*transfer)->submitAndWaitExpected())
            {
                return std::unexpected("Readback after compute discard failed");
            }
            if (auto checked = checkReadback(device, **readback, cycle, false); !checked)
            {
                return checked;
            }
            copyCommands = (*transfer)->beginExpected();
            if (!copyCommands)
            {
                return std::unexpected(copyCommands.error());
            }
            // Both resources retain their tracked transfer states; only this unsubmitted copy is abandoned.
            device.core.CmdCopyBuffer(*copyCommands, (*readback)->handle, (*output)->handle, &copy);
            if (!(*transfer)->discardExpected() || !(*transfer)->beginExpected() ||
                !(*transfer)->submitAndWaitExpected())
            {
                return std::unexpected("Cannot discard and restart a transfer encoder");
            }
            if (auto checked = checkReadback(device, **readback, cycle, false); !checked)
            {
                return checked;
            }
            std::printf("FRAME_QUEUES cycle=%u consumer_first=%d exact_values=64 compute_blit_discard_verified=1\n",
                        cycle,
                        int(nativeMetal));
            std::fflush(stdout);
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-frame-queues probe.vshaderc\n", stderr);
        return 2;
    }
    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0 || stream.tellg() > 64 * 1024 * 1024)
    {
        std::fputs("Cannot read queue probe shader\n", stderr);
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        std::fputs("Cannot read complete queue probe shader\n", stderr);
        return 1;
    }
    auto program = ShaderProgram::loadExpected(bytes, argv[1]);
    auto device  = Device::create(true);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    std::printf("FRAME_QUEUES_DEVICE native_metal=%d\n",
                int((*device)->core.GetDeviceDesc((*device)->handle)->graphicsAPI == VriGraphicsAPI_Metal));
    std::fflush(stdout);
    if ((*device)->core.GetDeviceDesc((*device)->handle)->graphicsAPI == VriGraphicsAPI_Metal)
    {
        auto frame = Frame::create(**device);
        if (!frame)
        {
            std::fprintf(stderr, "%s\n", frame.error().c_str());
            return 1;
        }
        for (unsigned i = 0; i < 256; ++i)
        {
            auto recorded = (*frame)->beginExpected();
            if (!recorded)
            {
                std::fprintf(stderr, "%s\n", recorded.error().c_str());
                return 1;
            }
            auto discarded = (*frame)->discardExpected();
            if (!discarded)
            {
                std::fprintf(stderr, "%s\n", discarded.error().c_str());
                return 1;
            }
        }
        std::puts("FRAME_QUEUES_METAL_DISCARD iterations=256");
    }
    if (auto result = probe(**device, *program); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Explicit graphics/compute/transfer fence chain and discard passed");
    return 0;
}
