#pragma once

#include "graph_heaps_probe.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace vultra::test::graph_host_memory_probe
{
    constexpr uint64_t localOffset = 64;
    constexpr uint64_t dataBytes   = 1024;

    inline uint32_t pattern(unsigned generation, unsigned resource, unsigned word)
    {
        return 0x81000000u | (generation << 16) | (resource << 12) | word;
    }

    inline std::expected<void, std::string> mappedWrites(Device& device, VriMemoryLocation location, bool heaps)
    {
        graph_heaps_probe::Fence             ready(nullptr, device.core.DestroyFence);
        RenderGraph                          graph(device);
        std::array<RenderGraph::Resource, 2> sources;
        std::array<RenderGraph::Resource, 2> outputs;
        unsigned                             generation = 1;
        bool                                 reject     = false;
        for (unsigned i = 0; i < sources.size(); ++i)
        {
            const auto suffix = std::to_string(i);
            auto       source = graph.createBufferExpected(
                "mapped.source-" + suffix,
                {localOffset + dataBytes,
                       0,
                       VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                       location});
            auto output = graph.createBufferExpected("mapped.output-" + suffix,
                                                     {dataBytes,
                                                      0,
                                                      VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                      VriMemoryLocation_HostReadback});
            if (!source || !output)
            {
                return std::unexpected("Create mapped graph resources failed");
            }
            sources[i] = *source;
            outputs[i] = *output;
            const RenderGraph::BufferRange range {localOffset, dataBytes};
            // Declare the complete replacement conservatively; the callback initializes it through CPU mapping.
            const std::array writeUses {RenderGraph::Use {*source, Usage::eCopyDestination, true, {}, range}};
            if (!graph.addPassExpected(
                    "mapped.prepare-" + suffix,
                    writeUses,
                    [&, i, resource = *source](auto*, auto& current) -> std::expected<void, std::string>
                    {
                        auto buffer = current.getBufferExpected(resource);
                        if (!buffer)
                        {
                            return std::unexpected(buffer.error());
                        }
                        auto* mapped =
                            static_cast<std::byte*>(device.core.MapBuffer((*buffer)->handle, localOffset, dataBytes));
                        if (!mapped)
                        {
                            return std::unexpected("Map a graph-owned host-visible buffer failed");
                        }
                        // CPU writes happen now, while all GPU copies are still unsubmitted recordings.
                        for (unsigned word = 0; word < dataBytes / sizeof(uint32_t); ++word)
                        {
                            const auto value = pattern(generation, i, word);
                            std::memcpy(mapped + word * sizeof(value), &value, sizeof(value));
                        }
                        device.core.UnmapBuffer((*buffer)->handle);
                        if (reject && i == 1)
                        {
                            return std::unexpected("injected mapped-write recording rejection");
                        }
                        return {};
                    }))
            {
                return std::unexpected("Add mapped graph writer failed");
            }
            const std::array copyUses {RenderGraph::Use {*source, Usage::eCopySource, false, {}, range},
                                       RenderGraph::Use {*output, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected(
                    "mapped.save-" + suffix,
                    copyUses,
                    [&, from = *source, to = *output](auto* commands, auto& current) -> std::expected<void, std::string>
                    {
                        auto input  = current.getBufferExpected(from);
                        auto output = current.getBufferExpected(to);
                        if (!input || !output)
                        {
                            return std::unexpected("Get mapped graph copy resources failed");
                        }
                        VriBufferCopyDesc copy {};
                        copy.srcOffset = localOffset;
                        copy.size      = dataBytes;
                        device.core.CmdCopyBuffer(commands, (*output)->handle, (*input)->handle, &copy);
                        return {};
                    }) ||
                !graph.exportResourceExpected(*output))
            {
                return std::unexpected("Add mapped graph reader failed");
            }
        }
        if (auto ordered = graph.addOrderingExpected("mapped.save-0", "mapped.prepare-1"); !ordered)
        {
            return ordered;
        }
        if (auto compiled = heaps ? graph.compileHeapsExpected() : graph.compileExpected(true); !compiled)
        {
            return compiled;
        }
        VriFence*  raw    = nullptr;
        const auto status = device.core.CreateFence(device.handle, 0, &raw);
        ready.reset(raw);
        if (status != VriResult_Success || !ready)
        {
            return std::unexpected("Create mapped graph queue dependency failed");
        }
        auto first  = Frame::create(device);
        auto second = Frame::create(device, VriQueueType_Transfer);
        if (!first || !second)
        {
            return std::unexpected("Create mapped graph frames failed");
        }
        uint64_t   checkedWords = 0;
        const auto verify       = [&](unsigned expectedGeneration) -> std::expected<void, std::string>
        {
            for (unsigned i = 0; i < outputs.size(); ++i)
            {
                auto buffer = graph.getBufferExpected(outputs[i]);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                const auto* mapped =
                    static_cast<const std::byte*>(device.core.MapBuffer((*buffer)->handle, 0, dataBytes));
                if (!mapped)
                {
                    return std::unexpected("Map completed host-write graph output failed");
                }
                bool valid = true;
                for (unsigned word = 0; word < dataBytes / sizeof(uint32_t); ++word)
                {
                    uint32_t actual = 0;
                    std::memcpy(&actual, mapped + word * sizeof(actual), sizeof(actual));
                    valid &= actual == pattern(expectedGeneration, i, word);
                    ++checkedWords;
                }
                device.core.UnmapBuffer((*buffer)->handle);
                if (!valid)
                {
                    return std::unexpected("A later CPU mapping overwrote an earlier unsubmitted GPU copy source");
                }
            }
            return {};
        };
        for (; generation <= 3; ++generation)
        {
            const bool routed = heaps && generation == 2;
            for (unsigned attempt = 0; attempt < (generation == 2 ? 2u : 1u); ++attempt)
            {
                auto commands = (*first)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                auto* later = *commands;
                if (routed)
                {
                    auto begun = (*second)->beginExpected();
                    if (!begun)
                    {
                        return std::unexpected(begun.error());
                    }
                    later = *begun;
                }
                const std::array recordings {RenderGraph::PassRecording {"mapped.prepare-0", *commands},
                                             RenderGraph::PassRecording {"mapped.save-0", *commands},
                                             RenderGraph::PassRecording {"mapped.prepare-1", later},
                                             RenderGraph::PassRecording {"mapped.save-1", later}};
                reject        = generation == 2 && attempt == 0;
                auto recorded = graph.executeExpected(*commands, recordings);
                if (reject)
                {
                    if (recorded || recorded.error().find("injected mapped-write") == std::string::npos ||
                        !(*first)->discardExpected() || (routed && !(*second)->discardExpected()))
                    {
                        return std::unexpected("Mapped-write recording failure did not reject/discard cleanly");
                    }
                    // Discard restores tracking, not mapped bytes. Completed outputs keep the prior generation.
                    if (auto previous = verify(generation - 1); !previous)
                    {
                        return previous;
                    }
                    continue;
                }
                if (!recorded)
                {
                    return recorded;
                }
                if (routed)
                {
                    const std::array dependency {
                        VriFenceSubmitDesc {ready.get(), generation, VriPipelineStage_AllCommands}};
                    if (!(*first)->submitExpected({}, dependency) || !(*second)->submitExpected(dependency) ||
                        !(*second)->waitExpected() || !(*first)->waitExpected())
                    {
                        return std::unexpected("Submit explicit mapped graph queue chain failed");
                    }
                }
                else if (auto completed = (*first)->submitAndWaitExpected(); !completed)
                {
                    return completed;
                }
                if (auto checked = verify(generation); !checked)
                {
                    return checked;
                }
            }
        }
        auto a = graph.resourceInfoExpected(sources[0]);
        auto b = graph.resourceInfoExpected(sources[1]);
        if (!a || !b || a->allocation == b->allocation ||
            (heaps &&
             (a->heap == UINT32_MAX || a->heap != b->heap ||
              !(a->heapOffset + a->heapBytes <= b->heapOffset || b->heapOffset + b->heapBytes <= a->heapOffset))))
        {
            return std::unexpected("Host-visible graph resources did not retain distinct storage regions");
        }
        std::printf(
            "GRAPH_HOST_MEMORY location=%u heaps=%u frames=3 destinations=%s recording_reject=1 buffer_words=%llu\n",
            unsigned(location),
            unsigned(heaps),
            heaps ? "1,2" : "1",
            static_cast<unsigned long long>(checkedWords));
        return {};
    }

    inline std::expected<void, std::string> probe(Device& device)
    {
        unsigned failures = 0;
        for (const auto location :
             {VriMemoryLocation_HostUpload, VriMemoryLocation_DeviceUpload, VriMemoryLocation_HostReadback})
        {
            for (const bool heaps : {false, true})
            {
                if (auto checked = mappedWrites(device, location, heaps); !checked)
                {
                    ++failures;
                    std::fprintf(stderr,
                                 "GRAPH_HOST_MEMORY_FAILURE location=%u heaps=%u: %s\n",
                                 unsigned(location),
                                 unsigned(heaps),
                                 checked.error().c_str());
                }
            }
        }
        if (failures)
        {
            return std::unexpected("Host-visible graph storage failed " + std::to_string(failures) + " models");
        }
        return {};
    }
} // namespace vultra::test::graph_host_memory_probe
