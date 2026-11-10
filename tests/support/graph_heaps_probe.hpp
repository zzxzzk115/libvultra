#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace vultra::test
{
    namespace graph_heaps_probe
    {
        using Inspect                = std::expected<void, std::string> (*)(const RenderGraph&);
        using Fence                  = std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)>;
        constexpr uint64_t dataBytes = 1024;

        struct BarrierFault
        {
            Device&                                           device;
            decltype(VriCoreInterface::CmdAliasMemoryBarrier) original;
            unsigned                                          calls    = 0;
            inline static BarrierFault*                       s_Active = nullptr;

            explicit BarrierFault(Device& gpu) :
                device(gpu),
                original(gpu.core.CmdAliasMemoryBarrier)
            {
                s_Active                          = this;
                device.core.CmdAliasMemoryBarrier = invoke;
            }

            ~BarrierFault()
            {
                device.core.CmdAliasMemoryBarrier = original;
                s_Active                          = nullptr;
            }

            static VriResult VRI_CALL invoke(VriCommandBuffer* commands)
            {
                if (++s_Active->calls == 2)
                {
                    return VriResult_Failure;
                }
                return s_Active->original(commands);
            }
        };

        struct RecordingFault
        {
            Device&                                      device;
            decltype(VriCoreInterface::EndCommandBuffer) end;
            decltype(VriCoreInterface::QueueSubmit)      submit;
            unsigned                                     calls    = 0;
            inline static RecordingFault*                s_Active = nullptr;

            RecordingFault(Device& gpu, bool rejectEnd) :
                device(gpu),
                end(gpu.core.EndCommandBuffer),
                submit(gpu.core.QueueSubmit)
            {
                s_Active = this;
                if (rejectEnd)
                {
                    device.core.EndCommandBuffer = failEnd;
                }
                else
                {
                    device.core.QueueSubmit = failSubmit;
                }
            }

            ~RecordingFault()
            {
                device.core.EndCommandBuffer = end;
                device.core.QueueSubmit      = submit;
                s_Active                     = nullptr;
            }

            static VriResult VRI_CALL failEnd(VriCommandBuffer*)
            {
                ++s_Active->calls;
                return VriResult_Failure;
            }

            static VriResult VRI_CALL failSubmit(VriQueue*, const VriQueueSubmitDesc*)
            {
                ++s_Active->calls;
                return VriResult_Failure;
            }
        };

        struct HeapCompileFault
        {
            Device&                                      device;
            bool                                         failBinding;
            decltype(VriCoreInterface::AllocateMemory)   allocate;
            decltype(VriCoreInterface::FreeMemory)       free;
            decltype(VriCoreInterface::BindBufferMemory) bind;
            decltype(VriCoreInterface::DestroyBuffer)    destroy;
            unsigned                                     allocations        = 0;
            unsigned                                     allocated          = 0;
            unsigned                                     bindings           = 0;
            unsigned                                     freed              = 0;
            VriBuffer*                                   bound              = nullptr;
            VriMemory*                                   boundMemory        = nullptr;
            bool                                         releasedWhileBound = false;
            inline static HeapCompileFault*              s_Active           = nullptr;

            HeapCompileFault(Device& gpu, bool binding) :
                device(gpu),
                failBinding(binding),
                allocate(gpu.core.AllocateMemory),
                free(gpu.core.FreeMemory),
                bind(gpu.core.BindBufferMemory),
                destroy(gpu.core.DestroyBuffer)
            {
                s_Active                     = this;
                device.core.AllocateMemory   = allocateMemory;
                device.core.FreeMemory       = freeMemory;
                device.core.BindBufferMemory = bindBuffer;
                device.core.DestroyBuffer    = destroyBuffer;
            }

            ~HeapCompileFault()
            {
                device.core.AllocateMemory   = allocate;
                device.core.FreeMemory       = free;
                device.core.BindBufferMemory = bind;
                device.core.DestroyBuffer    = destroy;
                s_Active                     = nullptr;
            }

            static VriResult VRI_CALL allocateMemory(VriDevice* gpu, const VriMemoryDesc* desc, VriMemory** result)
            {
                auto& fault = *s_Active;
                if (++fault.allocations == 2 && !fault.failBinding)
                {
                    *result = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto status = fault.allocate(gpu, desc, result);
                fault.allocated += unsigned(status == VriResult_Success && *result);
                return status;
            }

            static VriResult VRI_CALL bindBuffer(VriDevice* gpu, VriBuffer* buffer, VriMemory* memory, uint64_t offset)
            {
                auto& fault = *s_Active;
                if (++fault.bindings == 2 && fault.failBinding)
                {
                    return VriResult_Failure;
                }
                const auto status = fault.bind(gpu, buffer, memory, offset);
                if (status == VriResult_Success)
                {
                    fault.bound       = buffer;
                    fault.boundMemory = memory;
                }
                return status;
            }

            static void VRI_CALL destroyBuffer(VriBuffer* buffer)
            {
                auto& fault = *s_Active;
                if (buffer == fault.bound)
                {
                    fault.bound = nullptr;
                }
                fault.destroy(buffer);
            }

            static void VRI_CALL freeMemory(VriMemory* memory)
            {
                auto& fault = *s_Active;
                fault.releasedWhileBound |= memory == fault.boundMemory && fault.bound;
                ++fault.freed;
                fault.free(memory);
            }
        };

        inline std::expected<void, std::string> copyBuffer(Device&               device,
                                                           VriCommandBuffer*     commands,
                                                           RenderGraph&          graph,
                                                           RenderGraph::Resource source,
                                                           RenderGraph::Resource destination)
        {
            auto from = graph.getBufferExpected(source);
            auto to   = graph.getBufferExpected(destination);
            if (!from || !to)
            {
                return std::unexpected("Heap probe copy resource is unavailable");
            }
            VriBufferCopyDesc copy {};
            copy.size = dataBytes;
            device.core.CmdCopyBuffer(commands, (*to)->handle, (*from)->handle, &copy);
            return {};
        }

        inline std::expected<void, std::string> mixed(Device& device, Inspect inspect)
        {
            auto input = Buffer::create(device,
                                        {dataBytes,
                                         0,
                                         VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                         VriMemoryLocation_HostUpload});
            if (!input)
            {
                return std::unexpected(input.error());
            }
            RenderGraph graph(device);
            auto        imageDesc = colorTexture({16, 8}, VriFormat_RGBA8_UNORM);
            imageDesc.usage =
                VriTextureUsage_ColorAttachment | VriTextureUsage_TransferSrc | VriTextureUsage_ConcurrentQueues;
            const VriBufferDesc transfer {dataBytes,
                                          0,
                                          VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst |
                                              VriBufferUsage_ConcurrentQueues,
                                          VriMemoryLocation_Device};
            const auto          alignment = std::lcm(
                uint64_t(4),
                uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
            const auto rowBytes  = ((16 * 4 + alignment - 1) / alignment) * alignment;
            auto       oldImage  = graph.createTextureExpected("heap.old-image", imageDesc);
            auto       newBuffer = graph.createBufferExpected("heap.new-buffer", transfer);
            auto       bridge    = graph.createBufferExpected("bridge", transfer);
            auto       oldOutput = graph.createBufferExpected("old-output",
                                                              {rowBytes * 8,
                                                               0,
                                                               VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                               VriMemoryLocation_HostReadback});
            auto       newOutput = graph.createBufferExpected("new-output",
                                                              {dataBytes,
                                                               0,
                                                               VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                               VriMemoryLocation_HostReadback});
            auto       imported  = graph.importResourceExpected("input", **input);
            if (!oldImage || !newBuffer || !bridge || !oldOutput || !newOutput || !imported)
            {
                return std::unexpected("Create mixed heap graph resources failed");
            }
            std::array<float, 4> clear {1, 0, 0, 1};
            const std::array     clearUses {RenderGraph::Use {*oldImage, Usage::eColorWrite, true}};
            auto                 added = graph.addPassExpected(
                "clear-old",
                clearUses,
                [&, old = *oldImage](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    auto texture = current.getTextureExpected(old);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    auto view = (*texture)->viewExpected();
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    beginColorPass(device, commands, *view, {16, 8}, clear.data());
                    device.core.CmdEndRendering(commands);
                    return {};
                });
            const std::array saveUses {RenderGraph::Use {*oldImage, Usage::eCopySource},
                                       RenderGraph::Use {*oldOutput, Usage::eCopyDestination, true},
                                       RenderGraph::Use {*bridge, Usage::eCopyDestination, true},
                                       RenderGraph::Use {*imported, Usage::eCopySource}};
            if (!added ||
                !graph.addPassExpected(
                    "save-old",
                    saveUses,
                    [&, old = *oldImage, output = *oldOutput, source = *imported, token = *bridge](
                        auto* commands,
                        auto& current) -> std::expected<void, std::string>
                    {
                        auto texture = current.getTextureExpected(old);
                        auto buffer  = current.getBufferExpected(output);
                        if (!texture || !buffer)
                        {
                            return std::unexpected("Get old heap image/output failed");
                        }
                        VriBufferTextureCopyDesc copy {};
                        copy.bufferRowLength   = uint32_t(rowBytes / 4);
                        copy.bufferImageHeight = 8;
                        copy.texture.width     = 16;
                        copy.texture.height    = 8;
                        copy.texture.depth     = 1;
                        copy.texture.layerNum  = 1;
                        copy.texture.aspect    = VriImageAspect_Color;
                        device.core.CmdReadbackTextureToBuffer(commands, (*buffer)->handle, (*texture)->handle, &copy);
                        return copyBuffer(device, commands, current, source, token);
                    }))
            {
                return std::unexpected("Add old heap image passes failed");
            }
            const std::array replaceUses {RenderGraph::Use {*bridge, Usage::eCopySource},
                                          RenderGraph::Use {*newBuffer, Usage::eCopyDestination, true}};
            const std::array readUses {RenderGraph::Use {*newBuffer, Usage::eCopySource},
                                       RenderGraph::Use {*newOutput, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected("replace-buffer",
                                       replaceUses,
                                       [&, from = *bridge, to = *newBuffer](auto* commands, auto& current)
                                       {
                                           return copyBuffer(device, commands, current, from, to);
                                       }) ||
                !graph.addPassExpected("save-buffer",
                                       readUses,
                                       [&, from = *newBuffer, to = *newOutput](auto* commands, auto& current)
                                       {
                                           return copyBuffer(device, commands, current, from, to);
                                       }) ||
                !graph.addOrderingExpected("save-old", "save-buffer") || !graph.exportResourceExpected(*bridge) ||
                !graph.exportResourceExpected(*oldOutput) || !graph.exportResourceExpected(*newOutput))
            {
                return std::unexpected("Add replacement heap passes/exports failed");
            }
            auto compiled = graph.compileHeapsExpected();
            if (!compiled)
            {
                return std::unexpected(compiled.error());
            }
            const auto plan = graph.snapshot();
            const auto a    = graph.resourceInfo(*oldImage);
            const auto z    = graph.resourceInfo(*newBuffer);
            if (plan.heaps.size() != 1 || plan.heaps[0].resources.size() != 2 || a.heap != z.heap || a.heap != 0 ||
                a.allocation == z.allocation || a.lastUse >= z.firstUse ||
                plan.resources[oldImage->index].memoryKnown || plan.resources[newBuffer->index].memoryKnown ||
                graph.resourceInfo(*bridge).heap != UINT32_MAX || !graph.compileHeapsExpected())
            {
                return std::unexpected("Mixed heap plan did not retain distinct objects and one shared allocation");
            }
            std::array<Fence, 3> ready {Fence(nullptr, device.core.DestroyFence),
                                        Fence(nullptr, device.core.DestroyFence),
                                        Fence(nullptr, device.core.DestroyFence)};
            for (auto& fence : ready)
            {
                VriFence*  raw    = nullptr;
                const auto status = device.core.CreateFence(device.handle, 0, &raw);
                fence.reset(raw);
                if (status != VriResult_Success || !fence)
                {
                    return std::unexpected("Create heap dependency fence failed");
                }
            }
            auto graphics      = Frame::create(device);
            auto compute       = Frame::create(device, VriQueueType_Compute);
            auto transferFrame = Frame::create(device, VriQueueType_Transfer);
            if (!graphics || !compute || !transferFrame)
            {
                return std::unexpected("Create heap recording frames failed");
            }
            uint64_t   completedValue = 0;
            const auto verify         = [&](unsigned color, uint32_t pattern) -> std::expected<void, std::string>
            {
                auto image  = graph.getBufferExpected(*oldOutput);
                auto buffer = graph.getBufferExpected(*newOutput);
                if (!image || !buffer)
                {
                    return std::unexpected("Heap readback outputs disappeared");
                }
                auto* pixels = static_cast<const uint8_t*>(device.core.MapBuffer((*image)->handle, 0, rowBytes * 8));
                if (!pixels)
                {
                    return std::unexpected("Map heap image output failed");
                }
                bool matches = true;
                for (size_t y = 0; y < 8; ++y)
                {
                    for (size_t x = 0; x < 16; ++x)
                    {
                        for (size_t channel = 0; channel < 4; ++channel)
                        {
                            matches &= pixels[y * rowBytes + x * 4 + channel] ==
                                       ((channel == color || channel == 3) ? 255 : 0);
                        }
                    }
                }
                device.core.UnmapBuffer((*image)->handle);
                const auto* mapped = device.core.MapBuffer((*buffer)->handle, 0, dataBytes);
                if (!mapped)
                {
                    return std::unexpected("Map heap buffer output failed");
                }
                std::array<uint32_t, dataBytes / sizeof(uint32_t)> values {};
                std::memcpy(values.data(), mapped, dataBytes);
                device.core.UnmapBuffer((*buffer)->handle);
                if (!matches || !std::ranges::all_of(values,
                                                     [pattern](uint32_t value)
                                                     {
                                                         return value == pattern;
                                                     }))
                {
                    return std::unexpected("Mixed heap image/buffer GPU content mismatch");
                }
                return {};
            };
            for (unsigned cycle = 0; cycle < 6; ++cycle)
            {
                const unsigned color   = cycle % 3;
                const uint32_t pattern = 0x10203040u + cycle;
                clear                  = {0, 0, 0, 1};
                clear[color]           = 1;
                auto* mapped           = device.core.MapBuffer((*input)->handle, 0, dataBytes);
                if (!mapped)
                {
                    return std::unexpected("Map heap frame input failed");
                }
                std::array<uint32_t, dataBytes / sizeof(uint32_t)> values {};
                values.fill(pattern);
                std::memcpy(mapped, values.data(), dataBytes);
                device.core.UnmapBuffer((*input)->handle);
                if (cycle == 2)
                {
                    auto commands = (*graphics)->beginExpected();
                    if (!commands)
                    {
                        return std::unexpected(commands.error());
                    }
                    auto old = graph.getTextureExpected(*oldImage);
                    if (!old)
                    {
                        return std::unexpected(old.error());
                    }
                    const auto before   = (*old)->captureState();
                    bool       rejected = false;
                    {
                        BarrierFault fault(device);
                        rejected = !graph.executeExpected(*commands) && fault.calls == 2;
                    }
                    if (!rejected || !((*old)->captureState() == before) || !(*graphics)->discardExpected())
                    {
                        return std::unexpected("Late heap barrier failure did not restore/discard tracking");
                    }
                    if (auto unchanged = verify(1, 0x10203041u); !unchanged)
                    {
                        return unchanged;
                    }
                }
                if (cycle == 4)
                {
                    for (const bool rejectEnd : {true, false})
                    {
                        auto checkpoint       = graph.checkpointExpected();
                        auto rejectedCommands = (*graphics)->beginExpected();
                        if (!checkpoint || !rejectedCommands)
                        {
                            return std::unexpected("Prepare heap recording rejection checkpoint failed");
                        }
                        if (auto recorded = graph.executeExpected(*rejectedCommands); !recorded)
                        {
                            return recorded;
                        }
                        bool rejected = false;
                        {
                            RecordingFault fault(device, rejectEnd);
                            rejected = !(*graphics)->submitExpected() && fault.calls == 1;
                        }
                        if (!rejected || !(*graphics)->discardExpected())
                        {
                            return std::unexpected("Heap end/submit rejection reached GPU work or prevented discard");
                        }
                        if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
                        {
                            return restored;
                        }
                        if (auto unchanged = verify((cycle - 1) % 3, 0x10203040u + cycle - 1); !unchanged)
                        {
                            return unchanged;
                        }
                    }
                    std::puts("GRAPH_HEAPS_RECORDING_RECOVERY end_reject=1 submit_reject=1 checkpoint=restored");
                }
                auto commands = (*graphics)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                std::vector<VriFenceSubmitDesc> previous;
                if (completedValue)
                {
                    previous.push_back({ready[2].get(), completedValue, VriPipelineStage_AllCommands});
                }
                const std::array complete {
                    VriFenceSubmitDesc {ready[2].get(), completedValue + 1, VriPipelineStage_AllCommands}};
                if (cycle % 2 == 0)
                {
                    if (auto recorded = graph.executeExpected(*commands); !recorded)
                    {
                        return recorded;
                    }
                    if (auto submitted = (*graphics)->submitExpected(previous, complete); !submitted)
                    {
                        return submitted;
                    }
                }
                else
                {
                    auto computeCommands  = (*compute)->beginExpected();
                    auto transferCommands = (*transferFrame)->beginExpected();
                    if (!computeCommands || !transferCommands)
                    {
                        return std::unexpected("Begin routed heap commands failed");
                    }
                    const std::array destinations {RenderGraph::PassRecording {"clear-old", *commands},
                                                   RenderGraph::PassRecording {"save-old", *commands},
                                                   RenderGraph::PassRecording {"replace-buffer", *computeCommands},
                                                   RenderGraph::PassRecording {"save-buffer", *transferCommands}};
                    // The transfer reads after "save-old" through the intervening compute submission.
                    const std::array order {RenderGraph::RecordingDependency {*commands, *computeCommands},
                                            RenderGraph::RecordingDependency {*computeCommands, *transferCommands}};
                    if (auto recorded = graph.executeOrderedExpected(*commands, destinations, order); !recorded)
                    {
                        return recorded;
                    }
                    const std::array produced {
                        VriFenceSubmitDesc {ready[0].get(), completedValue + 1, VriPipelineStage_AllCommands}};
                    const std::array consumed {
                        VriFenceSubmitDesc {ready[1].get(), completedValue + 1, VriPipelineStage_AllCommands}};
                    if (auto submitted = (*graphics)->submitExpected(previous, produced); !submitted)
                    {
                        return submitted;
                    }
                    if (auto submitted = (*compute)->submitExpected(produced, consumed); !submitted)
                    {
                        return submitted;
                    }
                    if (auto submitted = (*transferFrame)->submitExpected(consumed, complete); !submitted)
                    {
                        return submitted;
                    }
                    if (!(*transferFrame)->waitExpected() || !(*compute)->waitExpected())
                    {
                        return std::unexpected("Wait routed heap frame failed");
                    }
                }
                if (auto waited = (*graphics)->waitExpected(); !waited)
                {
                    return waited;
                }
                ++completedValue;
                if (auto checked = verify(color, pattern); !checked)
                {
                    return checked;
                }
            }
            if (inspect)
            {
                if (auto checked = inspect(graph); !checked)
                {
                    return checked;
                }
            }
            std::puts("GRAPH_HEAPS_MIXED frames=6 destinations=1,3 color_bytes=3072 buffer_words=1536 late_barrier=1");
            return {};
        }

        inline std::expected<void, std::string> allocationRecovery(Device& device)
        {
            RenderGraph graph(device);
            auto        first =
                graph.createBufferExpected("allocation.device",
                                           {dataBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_Device});
            auto second =
                graph.createBufferExpected("allocation.host",
                                           {dataBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!first || !second)
            {
                return std::unexpected("Create allocation recovery resources failed");
            }
            const std::array uses {RenderGraph::Use {*first, Usage::eCopyDestination, true},
                                   RenderGraph::Use {*second, Usage::eCopyDestination, true}};
            // Compilation-only failure injection; this graph is never recorded or submitted.
            if (!graph.addPassExpected(
                    "allocation.retain",
                    uses,
                    [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, std::string>
                    {
                        return {};
                    },
                    true))
            {
                return std::unexpected("Add allocation recovery pass failed");
            }
            {
                HeapCompileFault fault(device, false);
                auto             failed = graph.compileHeapsExpected();
                if (failed || failed.error().find("Allocate graph heap") == std::string::npos ||
                    fault.allocations != 2 || fault.allocated != 1 || fault.freed != 1 || fault.bindings ||
                    graph.aliasesTransients() || graph.getBufferExpected(*first) || graph.getBufferExpected(*second))
                {
                    return std::unexpected("Second-allocation rejection did not release the tentative first heap");
                }
            }
            if (auto compiled = graph.compileHeapsExpected(); !compiled)
            {
                return compiled;
            }
            auto captured   = graph.snapshotExpected();
            auto firstInfo  = graph.resourceInfoExpected(*first);
            auto secondInfo = graph.resourceInfoExpected(*second);
            if (!captured || !firstInfo || !secondInfo || captured->heaps.size() != 2 ||
                firstInfo->heap == secondInfo->heap)
            {
                return std::unexpected("Distinct memory locations were packed into one heap");
            }
            std::puts("GRAPH_HEAPS_ALLOCATE_RECOVERY allocation_failure=2 retry=1 locations=2");
            return {};
        }

        inline std::expected<void, std::string> forkedBuffers(Device& device)
        {
            const VriBufferDesc uploadDesc {dataBytes,
                                            0,
                                            VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                            VriMemoryLocation_HostUpload};
            auto                seedA = Buffer::create(device, uploadDesc);
            auto                seedB = Buffer::create(device, uploadDesc);
            auto                seedC = Buffer::create(device, uploadDesc);
            if (!seedA || !seedB || !seedC)
            {
                return std::unexpected("Create forked heap inputs failed");
            }
            for (const auto& [buffer, pattern] : std::array {std::pair {seedA->get(), 0x11111111u},
                                                             std::pair {seedB->get(), 0x77777777u},
                                                             std::pair {seedC->get(), 0x33333333u}})
            {
                auto* mapped = device.core.MapBuffer(buffer->handle, 0, dataBytes);
                if (!mapped)
                {
                    return std::unexpected("Map forked heap input failed");
                }
                std::array<uint32_t, dataBytes / sizeof(uint32_t)> data {};
                data.fill(pattern);
                std::memcpy(mapped, data.data(), dataBytes);
                device.core.UnmapBuffer(buffer->handle);
            }
            RenderGraph         graph(device);
            const VriBufferDesc transient {dataBytes,
                                           0,
                                           VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst |
                                               VriBufferUsage_ConcurrentQueues,
                                           VriMemoryLocation_Device};
            const VriBufferDesc output {dataBytes,
                                        0,
                                        VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                        VriMemoryLocation_HostReadback};
            auto                a      = graph.createBufferExpected("fork.old", transient);
            auto                b      = graph.createBufferExpected("fork.new", transient);
            auto                successor       = graph.createBufferExpected("fork.successor", transient);
            auto                successorOutput = graph.createBufferExpected("fork.successor-output", output);
            auto                bridge = graph.createBufferExpected("fork.bridge", transient);
            auto                late   = graph.createBufferExpected("fork.late", output);
            auto                early  = graph.createBufferExpected("fork.early", output);
            auto                result = graph.createBufferExpected("fork.result", output);
            auto                inputA = graph.importResourceExpected("fork.input-a", **seedA);
            auto                inputB = graph.importResourceExpected("fork.input-b", **seedB);
            auto                inputC          = graph.importResourceExpected("fork.input-c", **seedC);
            if (!a || !b || !successor || !successorOutput || !bridge || !late || !early || !result || !inputA ||
                !inputB)
            {
                return std::unexpected("Create forked heap graph failed");
            }
            const auto addCopy = [&](const char* name, RenderGraph::Resource from, RenderGraph::Resource to)
            {
                const std::array uses {RenderGraph::Use {from, Usage::eCopySource},
                                       RenderGraph::Use {to, Usage::eCopyDestination, true}};
                return graph.addPassExpected(name,
                                             uses,
                                             [&, from, to](auto* commands, auto& current)
                                             {
                                                 return copyBuffer(device, commands, current, from, to);
                                             });
            };
            if (!addCopy("fork-write", *inputA, *a) || !addCopy("fork-late-reader", *a, *late))
            {
                return std::unexpected("Add forked heap producer/reader failed");
            }
            const std::array uses {RenderGraph::Use {*a, Usage::eCopySource},
                                   RenderGraph::Use {*early, Usage::eCopyDestination, true},
                                   RenderGraph::Use {*inputB, Usage::eCopySource},
                                   RenderGraph::Use {*bridge, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected("fork-token-reader",
                                       uses,
                                       [&, from = *a, to = *early, seed = *inputB, token = *bridge](
                                           auto* commands,
                                           auto& current) -> std::expected<void, std::string>
                                       {
                                           if (auto copied = copyBuffer(device, commands, current, from, to); !copied)
                                           {
                                               return copied;
                                           }
                                           return copyBuffer(device, commands, current, seed, token);
                                       }) ||
                !addCopy("fork-replace", *bridge, *b) || !addCopy("fork-save", *b, *result) ||
                !addCopy("fork-successor", *inputC, *successor) ||
                !addCopy("fork-save-successor", *successor, *successorOutput) ||
                !graph.addOrderingExpected("fork-save", "fork-successor"))
            {
                return std::unexpected("Add forked heap consumers failed");
            }
            for (const auto resource : {*bridge, *late, *early, *result, *successorOutput})
            {
                if (auto exported = graph.exportResourceExpected(resource); !exported)
                {
                    return exported;
                }
            }
            {
                HeapCompileFault fault(device, true);
                auto             failed = graph.compileHeapsExpected();
                if (failed || failed.error().find("bind graph heap") == std::string::npos || fault.allocations != 1 ||
                    fault.allocated != 1 || fault.bindings != 2 || fault.freed != fault.allocated ||
                    fault.releasedWhileBound || fault.bound || graph.aliasesTransients() ||
                    graph.getBufferExpected(*a) || graph.getBufferExpected(*b) || graph.checkpointExpected())
                {
                    return std::unexpected("Failed heap binding published resources or violated cleanup ordering");
                }
            }
            std::puts("GRAPH_HEAPS_BIND_RECOVERY binding_failure=2 cleanup=resource_before_heap");
            if (auto compiled = graph.compileHeapsExpected(); !compiled)
            {
                return compiled;
            }
            auto observedA = graph.resourceInfoExpected(*a);
            auto observedB = graph.resourceInfoExpected(*b);
            auto captured  = graph.snapshotExpected();
            if (!observedA || !observedB || !captured)
            {
                return std::unexpected("Inspect packed fork resources failed");
            }
            const auto& infoA = *observedA;
            const auto& infoB = *observedB;
            if (infoA.lastUse >= infoB.firstUse || infoA.heap != infoB.heap || infoA.heap == UINT32_MAX ||
                captured->heaps.size() != 1 || !infoA.heapBytes || !infoB.heapBytes ||
                !(infoA.heapOffset + infoA.heapBytes <= infoB.heapOffset ||
                  infoB.heapOffset + infoB.heapBytes <= infoA.heapOffset))
            {
                return std::unexpected("An unordered reader was reduced to a declaration-order lifetime");
            }
            auto successorInfo = graph.resourceInfoExpected(*successor);
            if (!successorInfo)
            {
                return std::unexpected(successorInfo.error());
            }
            const auto& infoSuccessor = *successorInfo;
            if (infoSuccessor.heap != infoB.heap || infoSuccessor.heapOffset != infoB.heapOffset ||
                infoSuccessor.heapBytes != infoB.heapBytes || infoSuccessor.heapOffset == 0 ||
                infoB.lastUse >= infoSuccessor.firstUse)
            {
                return std::unexpected("Ordered successor did not reuse the nonzero region after its final reader");
            }
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            Fence      ready(raw, device.core.DestroyFence);
            auto       primary = Frame::create(device);
            auto       delayed = Frame::create(device, VriQueueType_Transfer);
            if (status != VriResult_Success || !ready || !primary || !delayed)
            {
                return std::unexpected("Create forked heap frames failed");
            }
            auto commands        = (*primary)->beginExpected();
            auto delayedCommands = (*delayed)->beginExpected();
            if (!commands || !delayedCommands)
            {
                return std::unexpected("Begin forked heap frames failed");
            }
            const std::array destinations {RenderGraph::PassRecording {"fork-write", *commands},
                                           RenderGraph::PassRecording {"fork-late-reader", *delayedCommands},
                                           RenderGraph::PassRecording {"fork-token-reader", *commands},
                                           RenderGraph::PassRecording {"fork-replace", *commands},
                                           RenderGraph::PassRecording {"fork-save", *commands},
                                           RenderGraph::PassRecording {"fork-successor", *commands},
                                           RenderGraph::PassRecording {"fork-save-successor", *commands}};
            if (auto recorded = graph.executeExpected(*commands, destinations); !recorded)
            {
                return recorded;
            }
            const std::array signal {VriFenceSubmitDesc {ready.get(), 1, VriPipelineStage_AllCommands}};
            if (auto submitted = (*primary)->submitExpected({}, signal); !submitted)
            {
                return submitted;
            }
            // The first reader in declaration order runs after the replacement resource is written.
            if (auto submitted = (*delayed)->submitExpected(signal); !submitted)
            {
                return submitted;
            }
            if (!(*delayed)->waitExpected() || !(*primary)->waitExpected())
            {
                return std::unexpected("Wait forked heap frames failed");
            }
            for (const auto& [resource, pattern] : std::array {std::pair {*late, 0x11111111u},
                                                               std::pair {*early, 0x11111111u},
                                                               std::pair {*result, 0x77777777u},
                                                               std::pair {*successorOutput, 0x33333333u}})
            {
                auto buffer = graph.getBufferExpected(resource);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                const auto* mapped = device.core.MapBuffer((*buffer)->handle, 0, dataBytes);
                if (!mapped)
                {
                    return std::unexpected("Map forked heap output failed");
                }
                std::array<uint32_t, dataBytes / sizeof(uint32_t)> values {};
                std::memcpy(values.data(), mapped, dataBytes);
                device.core.UnmapBuffer((*buffer)->handle);
                if (!std::ranges::all_of(values,
                                         [pattern](uint32_t value)
                                         {
                                             return value == pattern;
                                         }))
                {
                    return std::unexpected("Forked reader observed a replacement heap resource");
                }
            }
            std::puts("GRAPH_HEAPS_FORK words=1024 heaps=1 disjoint_regions=2 nonzero_alias=1 delayed_reader=1");
            return {};
        }

        inline std::expected<void, std::string> ordering(Device& device)
        {
            RenderGraph graph(device);
            unsigned    calls = 0;
            for (const auto* name : {"order-a", "order-b", "order-c"})
            {
                if (!graph.addPassExpected(
                        name,
                        {},
                        [&](auto*, auto&) -> std::expected<void, std::string>
                        {
                            ++calls;
                            return {};
                        },
                        std::string_view(name) == "order-c"))
                {
                    return std::unexpected("Add ordering probe pass failed");
                }
            }
            if (graph.addOrderingExpected("unknown", "order-c") || graph.addOrderingExpected("order-b", "order-a") ||
                graph.addOrderingExpected("order-a", "order-a") || !graph.addOrderingExpected("order-a", "order-b") ||
                !graph.addOrderingExpected("order-b", "order-c") || !graph.addOrderingExpected("order-b", "order-c") ||
                !graph.compileHeapsExpected())
            {
                return std::unexpected("Ordering validation, idempotence or compilation failed");
            }
            const auto snapshot = graph.snapshot();
            if (graph.activePasses().size() != 3 || !snapshot.passes[1].contentDependencies.empty() ||
                !snapshot.passes[2].contentDependencies.empty() || graph.addOrderingExpected("order-a", "order-c"))
            {
                return std::unexpected("Control edges did not retain work separately from content dependencies");
            }
            auto a = Frame::create(device);
            auto b = Frame::create(device, VriQueueType_Compute);
            if (!a || !b)
            {
                return std::unexpected("Create ordering probe frames failed");
            }
            auto first  = (*a)->beginExpected();
            auto second = (*b)->beginExpected();
            if (!first || !second)
            {
                return std::unexpected("Begin ordering probe frames failed");
            }
            const std::array cyclic {RenderGraph::PassRecording {"order-a", *first},
                                     RenderGraph::PassRecording {"order-b", *second},
                                     RenderGraph::PassRecording {"order-c", *first}};
            auto             rejected = graph.executeExpected(*first, cyclic);
            if (rejected || rejected.error().find("cyclic submission") == std::string::npos || calls != 0 ||
                !(*a)->discardExpected() || !(*b)->discardExpected())
            {
                return std::unexpected("Cyclic command grouping reached callbacks or prevented discard");
            }
            first = (*a)->beginExpected();
            if (!first || !graph.executeExpected(*first) || !(*a)->submitAndWaitExpected() || calls != 3)
            {
                return std::unexpected("Ordered work did not recover on one command buffer");
            }
            std::puts("GRAPH_ORDERING retained=3 content_edges=0 cyclic_reject=1 retry=1");
            return {};
        }

        inline std::expected<void, std::string> historyGrouping(Device& device)
        {
            RenderGraph graph(device);
            auto        desc = colorTexture({2, 2}, VriFormat_R32_UINT);
            desc.mipNum      = 2;
            desc.usage |= VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ConcurrentQueues;
            VriClearColor initial {};
            initial.u32[0]     = 17;
            auto history =
                graph.createHistoryTextureExpected("grouped-history", desc, VriClearValue {.color = initial});
            const auto row     = std::lcm(
                uint64_t(8),
                uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
            auto output = graph.createBufferExpected("history-output",
                                                     {row * 2,
                                                      0,
                                                      VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                      VriMemoryLocation_HostReadback});
            if (!history || !output)
            {
                return std::unexpected("Create history command-grouping resources failed");
            }
            unsigned         calls = 0;
            const std::array uses {RenderGraph::Use {*history, Usage::eCopySource, false, {0, 1, 0, 1}},
                                   RenderGraph::Use {*output, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected(
                    "history.read",
                    uses,
                    [&](auto* commands, auto& current) -> std::expected<void, std::string>
                    {
                        ++calls;
                        auto texture = current.getTextureExpected(*history);
                        auto buffer  = current.getBufferExpected(*output);
                        if (!texture || !buffer)
                        {
                            return std::unexpected("Get history command-grouping resources failed");
                        }
                        VriBufferTextureCopyDesc copy {};
                        copy.bufferRowLength   = uint32_t(row / sizeof(uint32_t));
                        copy.bufferImageHeight = 2;
                        copy.texture.width     = 2;
                        copy.texture.height    = 2;
                        copy.texture.depth     = 1;
                        copy.texture.layerNum  = 1;
                        copy.texture.aspect    = VriImageAspect_Color;
                        device.core.CmdReadbackTextureToBuffer(commands, (*buffer)->handle, (*texture)->handle, &copy);
                        return {};
                    }) ||
                !graph.addPassExpected(
                    "history.follow",
                    {},
                    [&](auto*, auto&) -> std::expected<void, std::string>
                    {
                        ++calls;
                        return {};
                    },
                    true) ||
                !graph.addOrderingExpected("history.read", "history.follow") ||
                !graph.exportResourceExpected(*output) || !graph.compileExpected())
            {
                return std::unexpected("Build history command-grouping graph failed");
            }
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            Fence      ready(raw, device.core.DestroyFence);
            auto       graphics = Frame::create(device);
            auto       compute  = Frame::create(device, VriQueueType_Compute);
            if (status != VriResult_Success || !ready || !graphics || !compute)
            {
                return std::unexpected("Create history command-grouping frames failed");
            }
            const auto verify = [&](uint32_t expected) -> std::expected<void, std::string>
            {
                auto buffer = graph.getBufferExpected(*output);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                const auto* data = static_cast<const uint8_t*>(device.core.MapBuffer((*buffer)->handle, 0, row * 2));
                if (!data)
                {
                    return std::unexpected("Map history command-grouping readback failed");
                }
                bool matches = true;
                for (size_t y = 0; y < 2; ++y)
                {
                    for (size_t x = 0; x < 2; ++x)
                    {
                        uint32_t value;
                        std::memcpy(&value, data + y * row + x * sizeof(value), sizeof(value));
                        matches &= value == expected;
                    }
                }
                device.core.UnmapBuffer((*buffer)->handle);
                if (!matches)
                {
                    return std::unexpected("History command grouping changed retained or initialized GPU values");
                }
                return {};
            };
            const auto rejectCycle = [&]() -> std::expected<void, std::string>
            {
                auto first  = (*graphics)->beginExpected();
                auto second = (*compute)->beginExpected();
                if (!first || !second)
                {
                    return std::unexpected("Begin cyclic history recordings failed");
                }
                const std::array recordings {RenderGraph::PassRecording {"history.read", *second},
                                             RenderGraph::PassRecording {"history.follow", *first}};
                calls         = 0;
                auto rejected = graph.executeExpected(*first, recordings);
                if (rejected || rejected.error().find("cyclic submission") == std::string::npos)
                {
                    return std::unexpected("Legacy routed recording accepted a pending-history cycle");
                }
                const std::array reverse {RenderGraph::RecordingDependency {*second, *first}};
                const std::array cyclic {reverse.front(), RenderGraph::RecordingDependency {*first, *second}};
                const std::array unknown {RenderGraph::RecordingDependency {*first, nullptr}};
                const std::array self {RenderGraph::RecordingDependency {*first, *first}};
                const auto       missesHistory = graph.executeOrderedExpected(*first, recordings, reverse);
                const auto       hasCycle      = graph.executeOrderedExpected(*first, recordings, cyclic);
                const auto       hasUnknown    = graph.executeOrderedExpected(*first, recordings, unknown);
                const auto       hasSelf       = graph.executeOrderedExpected(*first, recordings, self);
                const auto       info          = graph.historyInfoExpected(*history, {0, 1, 0, 1});
                if (missesHistory ||
                    missesHistory.error().find("misses a required submission dependency") == std::string::npos ||
                    hasCycle || hasCycle.error().find("cyclic submission") == std::string::npos || hasUnknown ||
                    hasUnknown.error().find("unknown command buffer") == std::string::npos || hasSelf ||
                    hasSelf.error().find("distinct command buffers") == std::string::npos || calls || !info ||
                    !info->resetPending || !(*graphics)->discardExpected() || !(*compute)->discardExpected())
                {
                    return std::unexpected("Pending history grouping reached callbacks or lost its clear on rejection");
                }
                return {};
            };
            const auto single = [&](uint32_t expected) -> std::expected<void, std::string>
            {
                auto commands = (*graphics)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                calls = 0;
                if (auto recorded = graph.executeExpected(*commands); !recorded)
                {
                    return recorded;
                }
                if (auto completed = (*graphics)->submitAndWaitExpected(); !completed)
                {
                    return completed;
                }
                if (calls != 2)
                {
                    return std::unexpected("History single-recording retry did not execute both callbacks");
                }
                return verify(expected);
            };
            if (auto rejected = rejectCycle(); !rejected)
            {
                return rejected;
            }
            if (auto initialized = single(17); !initialized)
            {
                return initialized;
            }
            auto texture  = graph.getTextureExpected(*history);
            auto commands = (*graphics)->beginExpected();
            if (!texture || !commands)
            {
                return std::unexpected("Prepare externally written history cell failed");
            }
            VriClearColor changed {};
            changed.u32[0] = 55;
            if (auto written = (*texture)->clearStorageExpected(*commands, changed, {0, 1, 0, 1}); !written)
            {
                return written;
            }
            const std::array writtenReady {VriFenceSubmitDesc {ready.get(), 1, VriPipelineStage_AllCommands}};
            if (!(*graphics)->submitExpected({}, writtenReady) || !(*graphics)->waitExpected())
            {
                return std::unexpected("Submit externally written history cell failed");
            }
            const std::array unrelated {RenderGraph::HistorySelection {*history, {1, 1, 0, 1}}};
            if (auto reset = graph.resetHistoryExpected(unrelated); !reset)
            {
                return reset;
            }
            auto first  = (*graphics)->beginExpected();
            auto second = (*compute)->beginExpected();
            if (!first || !second)
            {
                return std::unexpected("Begin unrelated history clear recordings failed");
            }
            const std::array recordings {RenderGraph::PassRecording {"history.read", *second},
                                         RenderGraph::PassRecording {"history.follow", *first}};
            calls = 0;
            const auto missingPassOrder = graph.executeOrderedExpected(*first, recordings, {});
            if (missingPassOrder ||
                missingPassOrder.error().find("misses a required submission dependency") == std::string::npos || calls)
            {
                return std::unexpected("Routed recording accepted an empty order for dependent passes");
            }
            const std::array order {RenderGraph::RecordingDependency {*second, *first}};
            if (auto recorded = graph.executeOrderedExpected(*first, recordings, order); !recorded)
            {
                return recorded;
            }
            const std::array dependency {VriFenceSubmitDesc {ready.get(), 2, VriPipelineStage_AllCommands}};
            if (!(*compute)->submitExpected(writtenReady, dependency) || !(*graphics)->submitExpected(dependency) ||
                !(*graphics)->waitExpected() || !(*compute)->waitExpected() || calls != 2)
            {
                return std::unexpected("Submit unrelated history clear and ordered reader failed");
            }
            if (auto retained = verify(55); !retained)
            {
                return retained;
            }
            const std::array selected {RenderGraph::HistorySelection {*history, {0, 1, 0, 1}}};
            if (auto reset = graph.resetHistoryExpected(selected); !reset)
            {
                return reset;
            }
            if (auto rejected = rejectCycle(); !rejected)
            {
                return rejected;
            }
            if (auto recovered = single(17); !recovered)
            {
                return recovered;
            }
            std::puts("GRAPH_HISTORY_GROUPING cyclic_reject=2 unrelated_mip=retained selected_mip=reset words=12");
            return {};
        }

        enum class FanoutLayout
        {
            eProducer,
            eGraphOrder,
            eSubmissionOrder,
            eDifferentRead,
            eTemporaryChange,
            eTemporaryProducer,
            eUndeclaredMip
        };

        inline std::expected<void, std::string> textureFanout(Device& device, FanoutLayout mode)
        {
            RenderGraph graph(device);
            auto        desc = colorTexture({4, 4});
            const bool  undeclaredMip = mode == FanoutLayout::eUndeclaredMip;
            desc.mipNum               = undeclaredMip ? 2u : 1u;
            desc.usage |= VriTextureUsage_ConcurrentQueues;
            auto       texture = graph.createTextureExpected("fanout.image", desc);
            const auto row     = std::lcm(
                uint64_t(16),
                uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
            const VriBufferDesc outputDesc {row * 4,
                                            0,
                                            VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                            VriMemoryLocation_HostReadback};
            auto                outputA = graph.createBufferExpected("fanout.a", outputDesc);
            auto                outputB = graph.createBufferExpected("fanout.b", outputDesc);
            if (!texture || !outputA || !outputB)
            {
                return std::unexpected("Create texture fanout graph failed");
            }
            const bool       differentRead     = mode == FanoutLayout::eDifferentRead;
            const bool       temporaryChange   = mode == FanoutLayout::eTemporaryChange;
            const bool       temporaryProducer = mode == FanoutLayout::eTemporaryProducer;
            bool             changeUndeclared  = undeclaredMip;
            const bool       graphOrder        = mode == FanoutLayout::eGraphOrder;
            bool             serial            = graphOrder || mode == FanoutLayout::eSubmissionOrder;
            bool             prepared          = differentRead || temporaryChange || temporaryProducer || undeclaredMip;
            const std::array writeUses {RenderGraph::Use {*texture, Usage::eColorWrite, true}};
            if (!graph.addPassExpected(
                    "fanout-write",
                    writeUses,
                    [&, image = *texture](auto* commands, auto& current) -> std::expected<void, std::string>
                    {
                        auto value = current.getTextureExpected(image);
                        if (!value)
                        {
                            return std::unexpected(value.error());
                        }
                        const std::array<float, 4> clear {1, 0, 1, 1};
                        for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
                        {
                            auto view = (*value)->mipViewExpected(mip);
                            if (!view)
                            {
                                return std::unexpected(view.error());
                            }
                            beginColorPass(device, commands, *view, {4u >> mip, 4u >> mip}, clear.data());
                            device.core.CmdEndRendering(commands);
                        }
                        if (prepared)
                        {
                            VriTextureViewDesc whole {};
                            whole.viewType = VriTextureViewType_MaxEnum;
                            return (*value)->transitionExpected(
                                commands,
                                {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                                whole);
                        }
                        return {};
                    }))
            {
                return std::unexpected("Add fanout producer failed");
            }
            const auto reader = [&](const char* name, RenderGraph::Resource output, bool isSecond)
            {
                const auto       usage = isSecond && differentRead ? Usage::eSampled : Usage::eCopySource;
                const std::array uses {RenderGraph::Use {*texture, usage, false, {0, 1, 0, 1}},
                                       RenderGraph::Use {output, Usage::eCopyDestination, true}};
                return graph.addPassExpected(
                    name,
                    uses,
                    [&, image = *texture, output, isSecond](auto* commands,
                                                            auto& current) -> std::expected<void, std::string>
                    {
                        auto source = current.getTextureExpected(image);
                        auto target = current.getBufferExpected(output);
                        if (!source || !target)
                        {
                            return std::unexpected("Get fanout copy resources failed");
                        }
                        const bool temporary = isSecond ? temporaryChange || changeUndeclared : temporaryProducer;
                        VriTextureViewDesc changedView {};
                        changedView.viewType = VriTextureViewType_MaxEnum;
                        changedView.baseMip  = isSecond && changeUndeclared ? 1u : 0u;
                        changedView.mipNum   = 1;
                        if (temporary)
                        {
                            if (auto changed = (*source)->transitionExpected(
                                    commands,
                                    {VriAccess_None, VriLayout_General, VriPipelineStage_AllCommands},
                                    changedView);
                                !changed)
                            {
                                return changed;
                            }
                        }
                        if (temporary || (isSecond && differentRead))
                        {
                            if (auto restored = (*source)->transitionExpected(
                                    commands,
                                    {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                                    changedView);
                                !restored)
                            {
                                return restored;
                            }
                        }
                        VriBufferTextureCopyDesc copy {};
                        copy.bufferRowLength   = uint32_t(row / 4);
                        copy.bufferImageHeight = 4;
                        copy.texture           = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 4, 4, 1};
                        device.core.CmdReadbackTextureToBuffer(commands, (*target)->handle, (*source)->handle, &copy);
                        return {};
                    });
            };
            if (!reader("fanout-a", *outputA, false) || !reader("fanout-b", *outputB, true) ||
                !graph.exportResourceExpected(*outputA) || !graph.exportResourceExpected(*outputB))
            {
                return std::unexpected("Add fanout readers failed");
            }
            if (graphOrder && !graph.addOrderingExpected("fanout-a", "fanout-b"))
            {
                return std::unexpected("Order fanout layouts failed");
            }
            if (auto compiled = graph.compileHeapsExpected(); !compiled)
            {
                return compiled;
            }
            std::array<Fence, 2> ready {Fence(nullptr, device.core.DestroyFence),
                                        Fence(nullptr, device.core.DestroyFence)};
            for (auto& fence : ready)
            {
                VriFence*  raw    = nullptr;
                const auto status = device.core.CreateFence(device.handle, 0, &raw);
                fence.reset(raw);
                if (status != VriResult_Success || !fence)
                {
                    return std::unexpected("Create fanout queue fence failed");
                }
            }
            auto producer = Frame::create(device);
            auto a        = Frame::create(device, VriQueueType_Compute);
            auto b        = Frame::create(device, differentRead ? VriQueueType_Graphics : VriQueueType_Transfer);
            if (!producer || !a || !b)
            {
                return std::unexpected("Create fanout frames failed");
            }
            std::array<VriCommandBuffer*, 3> commands {};
            const auto                       begin = [&]() -> std::expected<void, std::string>
            {
                const std::array frames {producer->get(), a->get(), b->get()};
                for (size_t i = 0; i < frames.size(); ++i)
                {
                    auto result = frames[i]->beginExpected();
                    if (!result)
                    {
                        return std::unexpected(result.error());
                    }
                    commands[i] = *result;
                }
                return {};
            };
            const auto record = [&]()
            {
                const std::array destinations {RenderGraph::PassRecording {"fanout-write", commands[0]},
                                               RenderGraph::PassRecording {"fanout-a", commands[1]},
                                               RenderGraph::PassRecording {"fanout-b", commands[2]}};
                if (graphOrder)
                {
                    return graph.executeExpected(commands[0], destinations);
                }
                const std::array order {RenderGraph::RecordingDependency {commands[0], commands[1]},
                                        RenderGraph::RecordingDependency {commands[serial ? 1 : 0], commands[2]}};
                return graph.executeOrderedExpected(commands[0], destinations, order);
            };
            if (!begin())
            {
                return std::unexpected("Begin fanout frames failed");
            }
            if (!serial)
            {
                std::string_view reason = "layout transition conflicts with unordered access from fanout-a";
                if (mode == FanoutLayout::eProducer || temporaryProducer)
                {
                    reason = "layout transition from fanout-a";
                }
                else if (undeclaredMip)
                {
                    reason = "texture callback accessed an undeclared mip, layer or aspect";
                }
                auto failed = record();
                if (failed || failed.error().find(reason) == std::string::npos || !(*producer)->discardExpected() ||
                    !(*a)->discardExpected() || !(*b)->discardExpected())
                {
                    return std::unexpected("Unordered reader layout was accepted or not recoverable");
                }
                prepared = true;
                serial           = mode != FanoutLayout::eProducer && !undeclaredMip;
                changeUndeclared = false;
                if (!begin())
                {
                    return std::unexpected("Retry fanout frames failed");
                }
            }
            if (auto recorded = record(); !recorded)
            {
                return recorded;
            }
            const std::array produced {VriFenceSubmitDesc {ready[0].get(), 1, VriPipelineStage_AllCommands}};
            const std::array read {VriFenceSubmitDesc {ready[1].get(), 1, VriPipelineStage_AllCommands}};
            if (!(*producer)->submitExpected({}, produced) || !(*a)->submitExpected(produced, read) ||
                !(*b)->submitExpected(serial ? std::span<const VriFenceSubmitDesc>(read) :
                                               std::span<const VriFenceSubmitDesc>(produced)) ||
                !(*b)->waitExpected() || !(*a)->waitExpected() || !(*producer)->waitExpected())
            {
                return std::unexpected("Submit/wait fanout dependencies failed");
            }
            for (const auto output : {*outputA, *outputB})
            {
                auto buffer = graph.getBufferExpected(output);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                const auto* data = static_cast<const uint8_t*>(device.core.MapBuffer((*buffer)->handle, 0, row * 4));
                if (!data)
                {
                    return std::unexpected("Map fanout pixels failed");
                }
                bool matches = true;
                for (size_t y = 0; y < 4; ++y)
                {
                    for (size_t x = 0; x < 4; ++x)
                    {
                        for (size_t channel = 0; channel < 4; ++channel)
                        {
                            matches &= data[y * row + x * 4 + channel] == (channel == 1 ? 0 : 255);
                        }
                    }
                }
                device.core.UnmapBuffer((*buffer)->handle);
                if (!matches)
                {
                    return std::unexpected("Texture fanout layout/readback mismatch");
                }
            }
            std::printf("GRAPH_HEAPS_FANOUT layout_case=%u bytes=128\n", unsigned(mode));
            return {};
        }

        inline std::expected<void, std::string> depthStores(Device& device, bool readOnly)
        {
            const bool independent =
                readOnly && device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_Vulkan;
            RenderGraph graph(device);
            auto        desc = depthTexture({4, 4});
            desc.usage |= VriTextureUsage_TransferSrc;
            auto depth = graph.createTextureExpected("depth-stores", desc);
            if (!depth)
            {
                return std::unexpected(depth.error());
            }
            constexpr std::array<std::string_view, 3> names {"clear-depth", "store-depth-a", "store-depth-b"};
            for (const auto name : names)
            {
                const bool       clear = name == names.front();
                const auto       read  = readOnly ? Usage::eDepthReadOnly : Usage::eDepthRead;
                const std::array uses {RenderGraph::Use {*depth, clear ? Usage::eDepthWrite : read, clear}};
                if (auto added = graph.addPassExpected(
                        std::string(name),
                        uses,
                        [&, clear](auto* commands, auto& current) -> std::expected<void, std::string>
                        {
                            auto texture = current.getTextureExpected(*depth);
                            if (!texture)
                            {
                                return std::unexpected(texture.error());
                            }
                            auto view = (*texture)->viewExpected();
                            if (!view)
                            {
                                return std::unexpected(view.error());
                            }
                            VriAttachmentDesc attachment {};
                            attachment.view    = *view;
                            attachment.loadOp  = clear ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
                            attachment.storeOp = VriAttachmentStoreOp_Store;
                            attachment.readOnly = readOnly && !clear;
                            attachment.clearValue.depthStencil.depth = 0.5f;
                            VriAttachmentsDesc render {};
                            render.depth      = &attachment;
                            render.renderArea = {0, 0, 4, 4};
                            render.layerNum   = 1;
                            device.core.CmdBeginRendering(commands, &render);
                            device.core.CmdEndRendering(commands);
                            if (readOnly && clear)
                            {
                                // Establish the shared layout before either branch starts.
                                VriTextureViewDesc selection {};
                                selection.viewType = VriTextureViewType_MaxEnum;
                                return (*texture)->transitionExpected(
                                    commands,
                                    {VriAccess_DepthStencilAttachmentRead,
                                     VriLayout_DepthStencilReadOnly,
                                     VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests},
                                    selection);
                            }
                            return {};
                        },
                        true);
                    !added)
                {
                    return added;
                }
            }
            if (auto compiled = graph.compileExpected(false); !compiled)
            {
                return compiled;
            }
            VriFence*  raw         = nullptr;
            const auto fenceStatus = device.core.CreateFence(device.handle, 0, &raw);
            Fence      fence(raw, device.core.DestroyFence);
            if (fenceStatus != VriResult_Success || !fence)
            {
                return std::unexpected("Create depth store test fence failed");
            }
            std::array<std::unique_ptr<Frame>, names.size()> frames;
            for (auto& frame : frames)
            {
                auto created = Frame::create(device);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                frame = std::move(*created);
            }
            for (const bool serial : {false, true})
            {
                std::array<RenderGraph::PassRecording, names.size()> recordings;
                for (size_t i = 0; i < recordings.size(); ++i)
                {
                    auto begun = frames[i]->beginExpected();
                    if (!begun)
                    {
                        return std::unexpected(begun.error());
                    }
                    recordings[i] = {names[i], *begun};
                }
                const auto cmd = [&](size_t index)
                {
                    return recordings[index].commands;
                };
                const std::array order {RenderGraph::RecordingDependency {cmd(0), cmd(1)},
                                        RenderGraph::RecordingDependency {cmd(serial ? 1 : 0), cmd(2)}};
                auto             recorded = graph.executeOrderedExpected(cmd(0), recordings, order);
                if (serial || independent)
                {
                    if (!recorded)
                    {
                        return recorded;
                    }
                    const std::array produced {
                        VriFenceSubmitDesc {fence.get(), serial ? 3u : 1u, VriPipelineStage_AllCommands}};
                    const std::array stored {VriFenceSubmitDesc {fence.get(), 4, VriPipelineStage_AllCommands}};
                    const auto       signal =
                        serial ? std::span<const VriFenceSubmitDesc>(stored) : std::span<const VriFenceSubmitDesc>();
                    if (!frames[0]->submitExpected({}, produced) || !frames[1]->submitExpected(produced, signal) ||
                        !frames[2]->submitExpected(serial ? stored : produced) || !frames[0]->waitExpected() ||
                        !frames[1]->waitExpected() || !frames[2]->waitExpected())
                    {
                        return std::unexpected("Submit depth attachment readers failed");
                    }
                    auto texture = graph.getTextureExpected(*depth);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    auto readback = TextureReadback::start(device, **texture);
                    if (!readback)
                    {
                        return std::unexpected(readback.error());
                    }
                    auto                  data = (*readback)->dataExpected();
                    std::array<float, 16> values {};
                    if (!data || data->bytes.size() != sizeof(values))
                    {
                        return std::unexpected("Read back depth attachments failed");
                    }
                    std::memcpy(values.data(), data->bytes.data(), sizeof(values));
                    if (std::ranges::count(values, 0.5f) != values.size())
                    {
                        return std::unexpected("Depth attachment readers changed the preserved depth");
                    }
                }
                else
                {
                    if (recorded || recorded.error().find("texture write from store-depth-a") == std::string::npos)
                    {
                        return std::unexpected(recorded ?
                                                   "Depth attachment stores were treated as simultaneous readers" :
                                                   "Depth attachment recording failed: " + recorded.error());
                    }
                    for (auto& frame : frames)
                    {
                        if (auto discarded = frame->discardExpected(); !discarded)
                        {
                            return discarded;
                        }
                    }
                }
            }
            if (readOnly)
            {
                std::printf("GRAPH_HEAPS_DEPTH_READ_ONLY exact_values=%d independent=%d serial=1\n",
                            independent ? 32 : 16,
                            int(independent));
            }
            else
            {
                std::puts("GRAPH_HEAPS_DEPTH_STORES exact_values=16 unordered_rejected=1");
            }
            return {};
        }

        struct TextureCommandProbe
        {
            Device&                                                      device;
            decltype(VriCoreInterface::CmdBarrier)                       barrier;
            decltype(VriCoreInterface::CmdClearDepthStencilTextureRange) clear;
            unsigned                                                     barriers = 0;
            unsigned                                                     clears   = 0;
            inline static TextureCommandProbe*                           s_Active = nullptr;

            explicit TextureCommandProbe(Device& gpu) :
                device(gpu),
                barrier(gpu.core.CmdBarrier),
                clear(gpu.core.CmdClearDepthStencilTextureRange)
            {
                s_Active                                     = this;
                device.core.CmdBarrier                       = countBarrier;
                device.core.CmdClearDepthStencilTextureRange = failClear;
            }

            ~TextureCommandProbe()
            {
                device.core.CmdBarrier                       = barrier;
                device.core.CmdClearDepthStencilTextureRange = clear;
                s_Active                                     = nullptr;
            }

            static void VRI_CALL countBarrier(VriCommandBuffer*, const VriBarrierGroupDesc* group)
            {
                s_Active->barriers += group->textureNum;
            }

            static VriResult VRI_CALL failClear(VriCommandBuffer*,
                                                VriTexture*,
                                                const VriClearDepthStencil*,
                                                const VriTextureSubresourceRange*,
                                                VriImageAspectFlags)
            {
                ++s_Active->clears;
                return VriResult_Failure;
            }
        };

        inline std::expected<void, std::string> depthAspectCallbacks(Device& device)
        {
            const auto* caps = device.core.GetDeviceDesc(device.handle);
            if (!caps->hasSeparateDepthStencilLayouts || !caps->hasClearDepthStencilTextureRange)
            {
                return std::unexpected(
                    "Depth/stencil callback fixture requires native separate layouts and range clears");
            }
            auto desc   = depthTexture({4, 4});
            desc.format = VriFormat_D32_SFLOAT_S8_UINT;
            desc.usage |= VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
            auto texture = Texture::create(device, desc);
            auto other   = Texture::create(device, desc);
            auto first   = Frame::create(device);
            auto second  = Frame::create(device);
            if (!texture || !other || !first || !second)
            {
                return std::unexpected("Allocate depth/stencil callback fixture failed");
            }
            auto initial = (*first)->beginExpected();
            if (!initial)
            {
                return std::unexpected(initial.error());
            }
            const VriAccessLayoutStage copySource {VriAccess_CopySourceRead,
                                                   VriLayout_CopySource,
                                                   VriPipelineStage_Transfer};
            if (auto cleared = (*texture)->clearDepthStencilExpected(*initial, {.25f, 37}); !cleared)
            {
                return cleared;
            }
            (*texture)->transition(*initial, copySource);
            if (!(*first)->submitExpected() || !(*first)->waitExpected())
            {
                return std::unexpected("Initialize depth/stencil callback fixture failed");
            }

            RenderGraph graph(device);
            auto        planes     = graph.importResourceExpected("planes", **texture);
            auto        undeclared = graph.importResourceExpected("unlisted", **other, false);
            auto        readback   = TextureReadback::create(device, **texture, 0, 0, VriImageAspect_Depth);
            if (!planes || !undeclared || !readback)
            {
                return std::unexpected("Prepare depth/stencil callback fixture failed");
            }
            const auto         initialState = (*texture)->captureState();
            const auto         otherState   = (*other)->captureState();
            VriTextureViewDesc stencilView {};
            stencilView.viewType      = VriTextureViewType_MaxEnum;
            stencilView.aspect        = VriImageAspect_Stencil;
            VriCommandBuffer* foreign = nullptr;
            enum class Case
            {
                eUndeclaredAspect,
                eUndeclaredTexture,
                eReadOnlyClear,
                eWrongCommandBuffer,
                eWholeState,
                eSnapshot,
                eWholeBarrier,
                eNativeClearFailure,
                eSuccess
            };
            Case                                                           mode = Case::eUndeclaredAspect;
            constexpr std::array<std::string_view, size_t(Case::eSuccess)> reasons {
                "planes: texture callback accessed an undeclared mip, layer or aspect",
                "unlisted: texture callback accessed an undeclared mip, layer or aspect",
                "planes: texture callback requested write access to a read-only mip, layer or aspect",
                "planes: texture callback used another recording destination",
                "planes: texture callback accessed an undeclared mip, layer or aspect",
                "planes: texture callback cannot restore a discarded recording",
                "planes: texture callback accessed an undeclared mip, layer or aspect",
                "planes: texture callback failed to record a depth/stencil clear"};
            const auto reader = [&](VriCommandBuffer* commands, RenderGraph&) -> std::expected<void, std::string>
            {
                if (mode == Case::eNativeClearFailure || mode == Case::eSuccess)
                {
                    return (*readback)->recordExpected(commands, **texture);
                }
                TextureCommandProbe probe(device);
                VriTextureViewDesc  depthView = stencilView;
                depthView.aspect              = VriImageAspect_Depth;
                switch (mode)
                {
                    case Case::eUndeclaredAspect:
                        (void)(*texture)->transitionExpected(commands, copySource, stencilView);
                        break;
                    case Case::eUndeclaredTexture:
                        (*other)->transition(commands, copySource);
                        break;
                    case Case::eReadOnlyClear:
                        (void)(*texture)->clearDepthStencilExpected(commands, {.75f, 83}, {}, VriImageAspect_Depth);
                        break;
                    case Case::eWrongCommandBuffer:
                        (void)(*texture)->transitionExpected(foreign, copySource, depthView);
                        break;
                    case Case::eWholeState:
                        (*texture)->setState(copySource);
                        break;
                    case Case::eSnapshot:
                        (void)(*texture)->restoreUnsubmittedExpected(initialState);
                        break;
                    case Case::eWholeBarrier:
                        (*texture)->transition(commands, copySource);
                        break;
                    case Case::eNativeClearFailure:
                    case Case::eSuccess:
                        break;
                }
                if (probe.barriers || probe.clears)
                {
                    return std::unexpected("Rejected texture callback reached a native command");
                }
                return {};
            };
            const auto writer = [&](VriCommandBuffer* commands, RenderGraph&) -> std::expected<void, std::string>
            {
                if (mode == Case::eNativeClearFailure)
                {
                    TextureCommandProbe probe(device);
                    (void)(*texture)->clearDepthStencilExpected(commands, {.75f, 83}, {}, VriImageAspect_Stencil);
                    if (probe.barriers != 1 || probe.clears != 1)
                    {
                        return std::unexpected("Native clear fault did not reach both calls");
                    }
                    return {};
                }
                return (*texture)->clearDepthStencilExpected(commands, {.75f, 83}, {}, VriImageAspect_Stencil);
            };
            const std::array depthUses {
                RenderGraph::Use {*planes, Usage::eDepthRead, false, {.aspect = VriImageAspect_Depth}}};
            const std::array stencilUses {
                RenderGraph::Use {*planes, Usage::eCopyDestination, true, {.aspect = VriImageAspect_Stencil}}};
            if (auto added = graph.addPassExpected("read-depth", depthUses, reader, true); !added)
            {
                return added;
            }
            if (auto added = graph.addPassExpected("write-stencil", stencilUses, writer, true); !added)
            {
                return added;
            }
            if (auto compiled = graph.compileExpected(false); !compiled)
            {
                return compiled;
            }

            for (size_t i = 0; i <= reasons.size(); ++i)
            {
                mode = Case(i);
                if (mode == Case::eSuccess)
                {
                    auto retained = TextureReadback::start(device, **texture, 0, 0, VriImageAspect_Stencil);
                    if (!retained)
                    {
                        return std::unexpected(retained.error());
                    }
                    auto bytes = (*retained)->dataExpected();
                    if (!bytes || bytes->bytes.size() != 16 || std::ranges::count(bytes->bytes, std::byte {37}) != 16)
                    {
                        return std::unexpected("Discarded callbacks changed the original stencil values");
                    }
                }
                auto depthCmd   = (*first)->beginExpected();
                auto stencilCmd = (*second)->beginExpected();
                if (!depthCmd || !stencilCmd)
                {
                    return std::unexpected("Begin depth/stencil callback recordings failed");
                }
                foreign = *stencilCmd;
                const std::array records {RenderGraph::PassRecording {"read-depth", *depthCmd},
                                          RenderGraph::PassRecording {"write-stencil", *stencilCmd}};
                auto             result = graph.executeOrderedExpected(*depthCmd, records, {});
                if (mode == Case::eSuccess)
                {
                    if (!result)
                    {
                        return result;
                    }
                    if (!(*first)->submitExpected() || !(*second)->submitExpected() || !(*first)->waitExpected() ||
                        !(*second)->waitExpected())
                    {
                        return std::unexpected("Submit disjoint depth/stencil callback recordings failed");
                    }
                    break;
                }
                if (result || result.error().find(reasons[i]) == std::string::npos)
                {
                    return std::unexpected("Depth/stencil callback rejection mismatch: " +
                                           (result ? std::string("unexpected success") : result.error()));
                }
                if (!(*first)->discardExpected() || !(*second)->discardExpected() ||
                    (*texture)->captureState() != initialState || (*other)->captureState() != otherState)
                {
                    return std::unexpected("Depth/stencil callback failure changed a texture checkpoint");
                }
            }

            auto depth   = (*readback)->dataExpected();
            auto stencil = TextureReadback::start(device, **texture, 0, 0, VriImageAspect_Stencil);
            if (!depth || !stencil)
            {
                return std::unexpected("Read back independent depth/stencil callback results failed");
            }
            auto                  stencilBytes = (*stencil)->dataExpected();
            std::array<float, 16> values {};
            if (depth->bytes.size() != sizeof(values) || !stencilBytes || stencilBytes->bytes.size() != values.size())
            {
                return std::unexpected("Unexpected depth/stencil callback readback sizes");
            }
            std::memcpy(values.data(), depth->bytes.data(), sizeof(values));
            if (std::ranges::count(values, .25f) != values.size() ||
                std::ranges::count(stencilBytes->bytes, std::byte {83}) != values.size())
            {
                return std::unexpected("Disjoint depth/stencil callbacks changed the preserved depth or lost stencil");
            }
            std::puts("GRAPH_HEAPS_ASPECT_CALLBACK rejected=8 unauthorized_native_commands=0 exact_values=48");
            return {};
        }
    } // namespace graph_heaps_probe

    inline std::expected<void, std::string> graphHeapsProbe(Device&                    device,
                                                            graph_heaps_probe::Inspect inspect = nullptr)
    {
        if (auto checked = graph_heaps_probe::mixed(device, inspect); !checked)
        {
            return checked;
        }
        if (auto checked = graph_heaps_probe::allocationRecovery(device); !checked)
        {
            return checked;
        }
        if (auto checked = graph_heaps_probe::forkedBuffers(device); !checked)
        {
            return checked;
        }
        if (auto checked = graph_heaps_probe::ordering(device); !checked)
        {
            return checked;
        }
        if (auto checked = graph_heaps_probe::historyGrouping(device); !checked)
        {
            return checked;
        }
        using enum graph_heaps_probe::FanoutLayout;
        for (const auto mode : {eProducer,
                                eGraphOrder,
                                eSubmissionOrder,
                                eDifferentRead,
                                eTemporaryChange,
                                eTemporaryProducer,
                                eUndeclaredMip})
        {
            if (auto checked = graph_heaps_probe::textureFanout(device, mode); !checked)
            {
                return checked;
            }
        }
        for (const bool readOnly : {false, true})
        {
            if (auto checked = graph_heaps_probe::depthStores(device, readOnly); !checked)
            {
                return checked;
            }
        }
        return graph_heaps_probe::depthAspectCallbacks(device);
    }
} // namespace vultra::test
