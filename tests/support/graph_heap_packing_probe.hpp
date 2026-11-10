#pragma once

#include "graph_heap_images_probe.hpp"
#include "graph_heaps_probe.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace vultra::test::graph_heap_packing_probe
{
    enum class MetadataFailure
    {
        eUnknownGranularity,
        eInvalidGranularity,
        eOverflow
    };

    struct MetadataFault
    {
        Device&                                         device;
        MetadataFailure                                 failure;
        VriDeviceDesc                                   caps;
        decltype(VriCoreInterface::GetDeviceDesc)       getCaps;
        decltype(VriCoreInterface::GetBufferMemoryDesc) getRequirements;
        decltype(VriCoreInterface::AllocateMemory)      allocate;
        unsigned                                        allocations = 0;
        inline static MetadataFault*                    s_Active    = nullptr;

        MetadataFault(Device& gpu, MetadataFailure kind) :
            device(gpu),
            failure(kind),
            caps(*gpu.core.GetDeviceDesc(gpu.handle)),
            getCaps(gpu.core.GetDeviceDesc),
            getRequirements(gpu.core.GetBufferMemoryDesc),
            allocate(gpu.core.AllocateMemory)
        {
            if (failure != MetadataFailure::eOverflow)
            {
                caps.bufferTextureGranularity = failure == MetadataFailure::eUnknownGranularity ? 0 : 3;
            }
            s_Active                        = this;
            device.core.GetDeviceDesc       = queryCaps;
            device.core.GetBufferMemoryDesc = queryRequirements;
            device.core.AllocateMemory      = rejectAllocation;
        }

        ~MetadataFault()
        {
            device.core.GetDeviceDesc       = getCaps;
            device.core.GetBufferMemoryDesc = getRequirements;
            device.core.AllocateMemory      = allocate;
            s_Active                        = nullptr;
        }

        static const VriDeviceDesc* VRI_CALL queryCaps(const VriDevice*)
        {
            return &s_Active->caps;
        }

        static void VRI_CALL queryRequirements(const VriDevice*     gpu,
                                               const VriBufferDesc* desc,
                                               VriMemoryLocation    location,
                                               VriMemoryDesc*       memory)
        {
            s_Active->getRequirements(gpu, desc, location, memory);
            if (s_Active->failure == MetadataFailure::eOverflow)
            {
                memory->size                        = std::numeric_limits<uint64_t>::max() - 255;
                memory->alignment                   = 256;
                memory->requiresDedicatedAllocation = false;
            }
        }

        static VriResult VRI_CALL rejectAllocation(VriDevice*, const VriMemoryDesc*, VriMemory** memory)
        {
            ++s_Active->allocations;
            *memory = nullptr;
            return VriResult_Failure;
        }
    };

    inline std::expected<void, std::string> metadataRejections(Device& device)
    {
        for (const auto failure :
             {MetadataFailure::eUnknownGranularity, MetadataFailure::eInvalidGranularity, MetadataFailure::eOverflow})
        {
            RenderGraph         graph(device);
            const VriBufferDesc desc {1024, 0, VriBufferUsage_TransferDst, VriMemoryLocation_Device};
            auto                a = graph.createBufferExpected("metadata.first", desc);
            auto                b = graph.createBufferExpected("metadata.second", desc);
            if (!a || !b)
            {
                return std::unexpected("Create heap metadata resources failed");
            }
            const std::array uses {RenderGraph::Use {*a, Usage::eCopyDestination, true},
                                   RenderGraph::Use {*b, Usage::eCopyDestination, true}};
            // No recording/submission: this checks rejection before any native heap allocation.
            if (!graph.addPassExpected(
                    "metadata.retain",
                    uses,
                    [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, std::string>
                    {
                        return {};
                    },
                    true))
            {
                return std::unexpected("Add heap metadata test pass failed");
            }
            {
                MetadataFault fault(device, failure);
                auto          compiled = graph.compileHeapsExpected();
                const auto    reason   = failure == MetadataFailure::eOverflow ? "overflows" : "granularity";
                if (compiled || compiled.error().find(reason) == std::string::npos || fault.allocations ||
                    graph.aliasesTransients() || graph.getBufferExpected(*a) || graph.getBufferExpected(*b))
                {
                    return std::unexpected("Invalid heap metadata allocated or published tentative resources");
                }
            }
            if (auto retry = graph.compileHeapsExpected(); !retry)
            {
                return retry;
            }
        }
        std::puts("GRAPH_HEAP_PACKING_METADATA granularity_reject=2 overflow_reject=1 native_allocations=0 retry=3");
        return {};
    }

    inline std::expected<void, std::string> packedRegions(Device& device, graph_heaps_probe::Inspect inspect = nullptr)
    {
        using graph_heaps_probe::dataBytes;
        std::array<std::unique_ptr<Buffer>, 2> inputs;
        std::vector<graph_heaps_probe::Fence>  ready;
        RenderGraph                            graph(device);
        auto arrayDesc     = graph_heap_images_probe::description(VriTextureType_2D, VriFormat_RGBA8_UNORM);
        arrayDesc.width    = 16;
        arrayDesc.height   = 8;
        arrayDesc.layerNum = 2;
        auto volumeDesc    = graph_heap_images_probe::description(VriTextureType_3D, VriFormat_R32_UINT);
        volumeDesc.depth   = 4;
        auto array         = graph_heap_images_probe::createImage(graph, "packed.array", arrayDesc);
        auto volume        = graph_heap_images_probe::createImage(graph, "packed.volume", volumeDesc);
        if (!array || !volume)
        {
            return std::unexpected("Create packed images failed");
        }
        const VriBufferDesc                  temporary {dataBytes,
                                       0,
                                       VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst |
                                           VriBufferUsage_ConcurrentQueues,
                                       VriMemoryLocation_Device};
        std::array<RenderGraph::Resource, 2> buffers;
        std::array<RenderGraph::Resource, 2> outputs;
        for (size_t i = 0; i < inputs.size(); ++i)
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
            inputs[i]   = std::move(*input);
            auto source = graph.importResourceExpected("packed.input-" + std::to_string(i), *inputs[i]);
            auto buffer = graph.createBufferExpected("packed.buffer-" + std::to_string(i), temporary);
            auto output = graph.createBufferExpected("packed.output-" + std::to_string(i),
                                                     {dataBytes,
                                                      0,
                                                      VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                      VriMemoryLocation_HostReadback});
            if (!source || !buffer || !output)
            {
                return std::unexpected("Create packed buffers failed");
            }
            buffers[i] = *buffer;
            outputs[i] = *output;
            if (auto exported = graph.exportResourceExpected(*output); !exported)
            {
                return exported;
            }
            const auto copy = [&](std::string name, RenderGraph::Resource from, RenderGraph::Resource to)
            {
                const std::array uses {RenderGraph::Use {from, Usage::eCopySource},
                                       RenderGraph::Use {to, Usage::eCopyDestination, true}};
                return graph.addPassExpected(
                    std::move(name),
                    uses,
                    [&, from, to](auto* commands, auto& current)
                    {
                        return graph_heaps_probe::copyBuffer(device, commands, current, from, to);
                    });
            };
            if (!copy("packed.write-" + std::to_string(i), *source, *buffer))
            {
                return std::unexpected("Add packed buffer copies failed");
            }
        }
        if (!graph_heap_images_probe::addImageClearPass(graph, *array, "packed.array") ||
            !graph_heap_images_probe::addImageClearPass(graph, *volume, "packed.volume"))
        {
            return std::unexpected("Add packed image writers failed");
        }
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            const std::array uses {RenderGraph::Use {buffers[i], Usage::eCopySource},
                                   RenderGraph::Use {outputs[i], Usage::eCopyDestination, true}};
            if (!graph.addPassExpected("packed.save-" + std::to_string(i),
                                       uses,
                                       [&, from = buffers[i], to = outputs[i]](auto* commands, auto& current)
                                       {
                                           return graph_heaps_probe::copyBuffer(device, commands, current, from, to);
                                       }))
            {
                return std::unexpected("Add packed buffer readers failed");
            }
        }
        if (!graph_heap_images_probe::addImageReadbackPass(graph, *array, "packed.array") ||
            !graph_heap_images_probe::addImageReadbackPass(graph, *volume, "packed.volume"))
        {
            return std::unexpected("Add packed image readers failed");
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }
        auto captured = graph.snapshotExpected();
        if (!captured)
        {
            return std::unexpected(captured.error());
        }
        const auto& snapshot = *captured;
        if (snapshot.heaps.size() != 1 || snapshot.heaps[0].resources.size() != 4)
        {
            return std::unexpected("Simultaneously live mixed resources did not share one native heap");
        }
        const std::array members {buffers[0], buffers[1], array->resource, volume->resource};
        const auto       granularity  = device.core.GetDeviceDesc(device.handle)->bufferTextureGranularity;
        bool             nonzeroImage = false;
        for (size_t i = 0; i < members.size(); ++i)
        {
            auto observed = graph.resourceInfoExpected(members[i]);
            if (!observed)
            {
                return std::unexpected(observed.error());
            }
            const auto& info = *observed;
            if (info.heap != 0 || !info.heapBytes || info.heapOffset > snapshot.heaps[0].size ||
                info.heapBytes > snapshot.heaps[0].size - info.heapOffset || info.memoryKnown)
            {
                return std::unexpected("Packed placement bounds or memory accounting are invalid");
            }
            nonzeroImage |= info.isTexture && info.heapOffset != 0;
            for (size_t j = 0; j < i; ++j)
            {
                auto previous = graph.resourceInfoExpected(members[j]);
                if (!previous)
                {
                    return std::unexpected(previous.error());
                }
                const auto& other = *previous;
                const auto  step  = info.isTexture == other.isTexture ? uint64_t(1) : granularity;
                if (info.heapOffset / step <= (other.heapOffset + other.heapBytes - 1) / step &&
                    other.heapOffset / step <= (info.heapOffset + info.heapBytes - 1) / step)
                {
                    return std::unexpected("Unordered packed resources overlap bytes or cross-kind native pages");
                }
            }
        }
        if (!nonzeroImage)
        {
            return std::unexpected("Packed image fixture did not exercise a nonzero native texture offset");
        }
        ready.reserve(3);
        for (size_t i = 0; i < 3; ++i)
        {
            VriFence*                raw     = nullptr;
            const auto               created = device.core.CreateFence(device.handle, 0, &raw);
            graph_heaps_probe::Fence fence(raw, device.core.DestroyFence);
            if (created != VriResult_Success || !fence)
            {
                return std::unexpected("Create packed writer completion fence failed");
            }
            ready.push_back(std::move(fence));
        }
        std::array<std::unique_ptr<Frame>, 6> frames;
        const std::array                      queues {VriQueueType_Graphics,
                                 VriQueueType_Compute,
                                 VriQueueType_Transfer,
                                 VriQueueType_Graphics,
                                 VriQueueType_Compute,
                                 VriQueueType_Transfer};
        for (size_t i = 0; i < frames.size(); ++i)
        {
            auto frame = Frame::create(device, queues[i]);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            frames[i] = std::move(*frame);
        }
        for (unsigned cycle = 0; cycle < 4; ++cycle)
        {
            std::array<uint32_t, 2> patterns {0x11223344u + cycle, 0x99887766u - cycle};
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                auto* mapped = device.core.MapBuffer(inputs[i]->handle, 0, dataBytes);
                if (!mapped)
                {
                    return std::unexpected("Map packed input failed");
                }
                std::array<uint32_t, dataBytes / sizeof(uint32_t)> data {};
                data.fill(patterns[i]);
                std::memcpy(mapped, data.data(), dataBytes);
                device.core.UnmapBuffer(inputs[i]->handle);
            }
            array->pixel  = graph_heap_images_probe::pixel(arrayDesc.format, cycle);
            volume->pixel = graph_heap_images_probe::pixel(volumeDesc.format, cycle + 1);
            std::array<VriCommandBuffer*, 6> commands {};
            const bool                       routed = cycle % 2 != 0;
            for (size_t i = 0; i < (routed ? frames.size() : 1); ++i)
            {
                auto started = frames[i]->beginExpected();
                if (!started)
                {
                    return std::unexpected(started.error());
                }
                commands[i] = *started;
            }
            std::expected<void, std::string> recorded;
            if (routed)
            {
                const std::array destinations {RenderGraph::PassRecording {"packed.write-0", commands[1]},
                                               RenderGraph::PassRecording {"packed.write-1", commands[1]},
                                               RenderGraph::PassRecording {"packed.array.clear", commands[0]},
                                               RenderGraph::PassRecording {"packed.volume.clear", commands[2]},
                                               RenderGraph::PassRecording {"packed.save-0", commands[4]},
                                               RenderGraph::PassRecording {"packed.save-1", commands[4]},
                                               RenderGraph::PassRecording {"packed.array.save", commands[3]},
                                               RenderGraph::PassRecording {"packed.volume.save", commands[5]}};
                recorded = graph.executeExpected(commands[0], destinations);
            }
            else
            {
                recorded = graph.executeExpected(commands[0]);
            }
            if (!recorded)
            {
                for (size_t i = 0; i < (routed ? frames.size() : 1); ++i)
                {
                    if (auto discarded = frames[i]->discardExpected(); !discarded)
                    {
                        return discarded;
                    }
                }
                return recorded;
            }
            const std::array signals {VriFenceSubmitDesc {ready[0].get(), cycle + 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {ready[1].get(), cycle + 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {ready[2].get(), cycle + 1, VriPipelineStage_AllCommands}};
            // All writes finish before any read. Incorrect aliasing cannot hide behind immediate per-branch saves.
            // The caller supplies these extra GPU waits; the graph's independent branches remain unordered.
            for (size_t i = 0; i < (routed ? frames.size() : 1); ++i)
            {
                std::span<const VriFenceSubmitDesc> waits;
                std::span<const VriFenceSubmitDesc> notify;
                if (routed)
                {
                    if (i < 3)
                    {
                        notify = {&signals[i], 1};
                    }
                    else
                    {
                        waits = signals;
                    }
                }
                if (auto submitted = frames[i]->submitExpected(waits, notify); !submitted)
                {
                    return submitted;
                }
            }
            for (size_t i = 0; i < (routed ? frames.size() : 1); ++i)
            {
                if (auto waited = frames[i]->waitExpected(); !waited)
                {
                    return waited;
                }
            }
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                auto output = graph.getBufferExpected(outputs[i]);
                if (!output)
                {
                    return std::unexpected(output.error());
                }
                const auto* mapped =
                    static_cast<const uint32_t*>(device.core.MapBuffer((*output)->handle, 0, dataBytes));
                if (!mapped)
                {
                    return std::unexpected("Map packed buffer output failed");
                }
                const bool equal = std::all_of(mapped,
                                               mapped + dataBytes / sizeof(uint32_t),
                                               [&](uint32_t value)
                                               {
                                                   return value == patterns[i];
                                               });
                device.core.UnmapBuffer((*output)->handle);
                if (!equal)
                {
                    return std::unexpected("A simultaneously live packed buffer overwrote another region");
                }
            }
            if (auto checked = graph_heap_images_probe::verify(graph, *array); !checked)
            {
                return checked;
            }
            if (auto checked = graph_heap_images_probe::verify(graph, *volume); !checked)
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
        std::printf("GRAPH_HEAP_PACKING frames=4 destinations=1,6 queues=1,3 resources=4 heaps=1 granularity=%llu "
                    "image_bytes=%llu buffer_words=2048\n",
                    static_cast<unsigned long long>(granularity),
                    static_cast<unsigned long long>(4 * (array->checkedBytes + volume->checkedBytes)));
        return {};
    }
} // namespace vultra::test::graph_heap_packing_probe
