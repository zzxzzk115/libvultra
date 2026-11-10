#pragma once

#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace vultra::test::graph_buffer_capture_probe
{
    constexpr uint64_t kBytes = 1024;
    using Result              = std::expected<void, std::string>;

    inline VriBufferDesc description()
    {
        return {kBytes,
                0,
                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                VriMemoryLocation_Device};
    }

    inline Result copy(RenderGraph&             graph,
                       std::string              name,
                       RenderGraph::Resource    input,
                       RenderGraph::Resource    output,
                       RenderGraph::BufferRange range  = {},
                       bool*                    reject = nullptr)
    {
        const std::array uses {RenderGraph::Use {input, Usage::eCopySource, false, {}, range},
                               RenderGraph::Use {output, Usage::eCopyDestination, true, {}, range}};
        return graph.addPassExpected(
            std::move(name),
            uses,
            [input, output, range, reject](auto* commands, auto& graph) -> Result
            {
                if (reject && *reject)
                {
                    return std::unexpected("Discard buffer snapshot recording");
                }
                auto source = graph.getBufferExpected(input);
                auto target = graph.getBufferExpected(output);
                if (!source || !target)
                {
                    return std::unexpected(source ? target.error() : source.error());
                }
                const VriBufferCopyDesc region {range.offset,
                                                range.offset,
                                                range.size ? range.size : (*source)->desc.size - range.offset};
                graph.device().core.CmdCopyBuffer(commands, (*target)->handle, (*source)->handle, &region);
                return {};
            },
            reject != nullptr);
    }

    inline Result validation(Device& device)
    {
        // Compilation-only fixtures: callbacks are never submitted.
        RenderGraph graph(device);
        RenderGraph foreign(device);
        auto        source = graph.createBufferExpected("source", description());
        auto        absent = graph.createBufferExpected("no-writer", description());
        auto        desc   = description();
        desc.usage         = VriBufferUsage_TransferDst;
        auto noTransfer    = graph.createBufferExpected("no-transfer", desc);
        auto texture       = graph.createTextureExpected("texture", colorTexture({2, 2}));
        if (!source || !absent || !noTransfer || !texture)
        {
            return std::unexpected("Create buffer capture validation declarations failed");
        }
        const std::array uses {RenderGraph::Use {*source, Usage::eCopyDestination, true, {}, {0, 32}},
                               RenderGraph::Use {*noTransfer, Usage::eCopyDestination, true},
                               RenderGraph::Use {*texture, Usage::eColorWrite, true}};
        const auto       empty = [](auto*, auto&) -> Result
        {
            return {};
        };
        if (!graph.addPassExpected("writer", uses, empty) || !graph.addPassExpected("duplicate", {}, empty) ||
            !graph.addPassExpected("duplicate", {}, empty) || !graph.addPassExpected("unrelated", {}, empty, true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Compile buffer capture validation prefix failed");
        }
        auto before = graph.snapshotExpected();
        if (!before)
        {
            return std::unexpected(before.error());
        }

        struct Rejected
        {
            const char*              writer;
            RenderGraph::Resource    resource;
            const char*              name;
            RenderGraph::BufferRange range;
        };

        const std::array rejected {Rejected {"writer", {&foreign, 0}, "foreign", {}},
                                   Rejected {"writer", *source, "", {}},
                                   Rejected {"missing", *source, "missing", {}},
                                   Rejected {"duplicate", *source, "duplicate", {}},
                                   Rejected {"unrelated", *source, "unrelated", {}},
                                   Rejected {"", *absent, "no-writer", {}},
                                   Rejected {"writer", *noTransfer, "no-transfer", {}},
                                   Rejected {"writer", *source, "empty-tail", {kBytes, 0}},
                                   Rejected {"writer", *source, "overflow", {13, UINT64_MAX}},
                                   Rejected {"writer", *source, "out-of-bounds", {13, kBytes}},
                                   Rejected {"writer", *source, "no-overlap", {64, 32}},
                                   Rejected {"", *source, "no-overlapping-writer", {64, 32}},
                                   Rejected {"writer", *texture, "texture-byte-range", {1, 1}}};
        for (const auto& request : rejected)
        {
            if (graph.captureAfterPassExpected(request.writer, request.resource, request.name, request.range))
            {
                return std::unexpected("Invalid buffer snapshot was accepted: " + std::string(request.name));
            }
            auto after = graph.snapshotExpected();
            if (!after || after->resources.size() != before->resources.size() ||
                after->passes.size() != before->passes.size())
            {
                return std::unexpected("Rejected buffer snapshot changed the compiled graph prefix");
            }
        }
        return {};
    }

    inline Result multiQueue(RenderGraph& graph)
    {
        auto&     device = graph.device();
        VriFence* raw    = nullptr;
        auto      status = device.core.CreateFence(device.handle, 0, &raw);
        std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(raw, device.core.DestroyFence);
        if (status != VriResult_Success || !fence)
        {
            return std::unexpected("Create buffer snapshot queue fence failed");
        }
        const auto                              names = graph.activePasses();
        std::vector<std::unique_ptr<Frame>>     frames;
        std::vector<RenderGraph::PassRecording> recordings;
        std::vector<RenderGraph::RecordingDependency> order;
        for (size_t index = 0; index < names.size(); ++index)
        {
            const std::array queues {VriQueueType_Graphics, VriQueueType_Compute, VriQueueType_Transfer};
            auto             frame = Frame::create(device, queues[index % queues.size()]);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (!recordings.empty())
            {
                order.push_back({recordings.back().commands, *commands});
            }
            recordings.push_back({names[index], *commands});
            frames.push_back(std::move(*frame));
        }
        if (auto recorded = graph.executeOrderedExpected(recordings.front().commands, recordings, order); !recorded)
        {
            return recorded;
        }
        for (size_t index = 0; index < frames.size(); ++index)
        {
            const std::array wait {VriFenceSubmitDesc {fence.get(), index, VriPipelineStage_AllCommands}};
            const std::array signal {VriFenceSubmitDesc {fence.get(), index + 1, VriPipelineStage_AllCommands}};
            const auto       waits =
                index ? std::span<const VriFenceSubmitDesc>(wait) : std::span<const VriFenceSubmitDesc>();
            if (auto submitted = frames[index]->submitExpected(waits, signal); !submitted)
            {
                return submitted;
            }
        }
        for (auto& frame : frames)
        {
            if (auto completed = frame->waitExpected(); !completed)
            {
                return completed;
            }
        }
        return {};
    }

    inline std::expected<uint64_t, std::string> snapshots(Device& device, unsigned mode)
    {
        auto uploadDesc           = description();
        uploadDesc.memoryLocation = VriMemoryLocation_HostUpload;
        auto first                = Buffer::create(device, uploadDesc);
        auto second               = Buffer::create(device, uploadDesc);
        if (!first || !second)
        {
            return std::unexpected("Create snapshot upload inputs failed");
        }
        RenderGraph graph(device);
        auto        a       = graph.importResourceExpected("A", **first);
        auto        b       = graph.importResourceExpected("B", **second);
        auto        working = graph.createBufferExpected("working", description());
        auto        late    = graph.createBufferExpected("late", description());
        bool        reject  = false;
        if (!a || !b || !working || !late || !copy(graph, "initial", *a, *working) ||
            !copy(graph, "inside", *b, *working, {13, 257}) || !copy(graph, "outside", *b, *working, {700, 64}))
        {
            return std::unexpected("Build snapshot source writers failed");
        }
        auto initial      = graph.captureAfterPassExpected("initial", *working, "initial-copy");
        auto selected     = graph.captureAfterPassExpected("inside", *working, "selected-copy", {13, 257});
        auto lastSelected = graph.captureAfterPassExpected("", *working, "last-selected-copy", {13, 257});
        auto tail         = graph.captureAfterPassExpected("initial", *working, "tail-copy", {900, 0});
        auto final        = graph.captureAfterPassExpected("", *working, "final-copy");
        if (!initial || !selected || !lastSelected || !tail || !final)
        {
            return std::unexpected("Append full/range/tail snapshots failed");
        }
        const std::array                             outputs {*initial, *selected, *lastSelected, *tail, *final};
        std::array<RenderGraph::Use, outputs.size()> joined;
        for (size_t index = 0; index < outputs.size(); ++index)
        {
            joined[index] = {outputs[index], Usage::eCopySource};
        }
        if (!graph.addPassExpected("join snapshots",
                                   joined,
                                   [](auto*, auto&) -> Result
                                   {
                                       return {};
                                   }) ||
            !copy(graph, "overwrite reused storage", *b, *late, {}, &reject) ||
            !graph.addOrderingExpected("join snapshots", "overwrite reused storage"))
        {
            return std::unexpected("Join snapshot lifetimes before reuse failed");
        }
        auto compiled = mode == 2 ? graph.compileHeapsExpected() : graph.compileExpected(mode == 1);
        if (!compiled)
        {
            return std::unexpected(compiled.error());
        }
        auto snapshot = graph.snapshotExpected();
        if (!snapshot)
        {
            return std::unexpected(snapshot.error());
        }
        const auto selectedWriter =
            std::ranges::find_if(snapshot->passes,
                                 [&](const auto& pass)
                                 {
                                     return std::ranges::any_of(pass.uses,
                                                                [&](const auto& use)
                                                                {
                                                                    return use.resourceIndex == lastSelected->index &&
                                                                           use.usage == Usage::eCopyDestination;
                                                                });
                                 });
        if (selectedWriter == snapshot->passes.end() || selectedWriter->name != "Capture after inside" ||
            (mode && graph.captureAfterPassExpected("initial", *working, "immutable-plan")))
        {
            return std::unexpected("Capture selected an unrelated writer or changed an immutable reuse plan");
        }
        auto original  = graph.resourceInfoExpected(*working);
        auto successor = graph.resourceInfoExpected(*late);
        if (!original || !successor || (mode == 1 && original->allocation != successor->allocation) ||
            (mode == 2 && (original->heap == UINT32_MAX || original->heap != successor->heap ||
                           original->heapOffset != successor->heapOffset)))
        {
            return std::unexpected("Snapshot fixture did not actually reuse the source storage");
        }
        for (auto output : outputs)
        {
            auto info = graph.resourceInfoExpected(output);
            if (!info || !info->exported || info->isTexture || info->heap != UINT32_MAX ||
                info->bufferDesc.structureStride || info->bufferDesc.memoryLocation != VriMemoryLocation_Device ||
                info->bufferDesc.usage != description().usage ||
                ((mode != 0) && info->allocation == original->allocation))
            {
                return std::unexpected("Captured bytes lost raw-buffer/export/independent-storage metadata");
            }
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        std::array<std::vector<std::byte>, outputs.size()> expected;
        uint64_t                                           bytes  = 0;
        const auto                                         verify = [&]() -> Result
        {
            for (size_t index = 0; index < outputs.size(); ++index)
            {
                auto buffer = graph.getBufferExpected(outputs[index]);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                auto captured = readbackBufferExpected(device, **buffer);
                if (!captured || captured->offset || captured->bytes != expected[index])
                {
                    return std::unexpected("Snapshot bytes differ after source overwrite/reuse/discard");
                }
                bytes += captured->bytes.size();
            }
            return {};
        };
        for (unsigned generation = 1; generation <= 3; ++generation)
        {
            std::array<std::vector<std::byte>, 2> patterns;
            for (size_t input = 0; input < patterns.size(); ++input)
            {
                patterns[input].resize(kBytes);
                for (size_t index = 0; index < kBytes; ++index)
                {
                    patterns[input][index] = std::byte((index * 17 + generation * 31 + input * 97) & 255);
                }
                auto& upload = input == 0 ? **first : **second;
                auto* mapped = device.core.MapBuffer(upload.handle, 0, kBytes);
                if (!mapped)
                {
                    return std::unexpected("Map snapshot pattern upload failed");
                }
                std::memcpy(mapped, patterns[input].data(), kBytes);
                device.core.UnmapBuffer(upload.handle);
            }
            if (generation == 2)
            {
                std::vector<Buffer::State> states;
                for (auto output : outputs)
                {
                    states.push_back((*graph.getBufferExpected(output))->captureState());
                }
                auto commands = (*frame)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                reject        = true;
                auto recorded = graph.executeExpected(*commands);
                reject        = false;
                if (recorded || recorded.error().find("Discard buffer snapshot recording") == std::string::npos ||
                    !(*frame)->discardExpected())
                {
                    return std::unexpected("Snapshot recording failure did not require discard");
                }
                for (size_t index = 0; index < outputs.size(); ++index)
                {
                    if ((*graph.getBufferExpected(outputs[index]))->captureState() != states[index])
                    {
                        return std::unexpected("Failed snapshot recording changed CPU resource state");
                    }
                }
                if (auto retained = verify(); !retained)
                {
                    return std::unexpected(retained.error());
                }
            }
            expected[0] = patterns[0];
            expected[1].assign(patterns[1].begin() + 13, patterns[1].begin() + 270);
            expected[2] = expected[1];
            expected[3].assign(patterns[0].begin() + 900, patterns[0].end());
            expected[4] = patterns[0];
            std::copy_n(patterns[1].begin() + 13, 257, expected[4].begin() + 13);
            std::copy_n(patterns[1].begin() + 700, 64, expected[4].begin() + 700);
            if (mode == 2 && generation == 3)
            {
                if (auto submitted = multiQueue(graph); !submitted)
                {
                    return std::unexpected(submitted.error());
                }
            }
            else
            {
                auto commands = (*frame)->beginExpected();
                if (!commands || !graph.executeExpected(*commands) || !(*frame)->submitAndWaitExpected())
                {
                    return std::unexpected("Submit snapshot generation failed");
                }
            }
            if (auto checked = verify(); !checked)
            {
                return std::unexpected(checked.error());
            }
        }
        return bytes;
    }

    inline Result selectedInitialization(Device& device)
    {
        auto uploadDesc           = description();
        uploadDesc.memoryLocation = VriMemoryLocation_HostUpload;
        auto upload               = Buffer::create(device, uploadDesc);
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        std::vector<std::byte> pattern(kBytes, std::byte {0x93});
        auto*                  mapped = device.core.MapBuffer((*upload)->handle, 0, kBytes);
        if (!mapped)
        {
            return std::unexpected("Map selected-only capture upload failed");
        }
        std::memcpy(mapped, pattern.data(), pattern.size());
        device.core.UnmapBuffer((*upload)->handle);
        for (bool whole : {false, true})
        {
            RenderGraph graph(device);
            auto        input   = graph.importResourceExpected("input", **upload);
            auto        working = graph.createBufferExpected("partly initialized", description());
            if (!input || !working || !copy(graph, "selected writer", *input, *working, {17, 521}))
            {
                return std::unexpected("Build selected-only capture writer failed");
            }
            auto captured = graph.captureAfterPassExpected("selected writer",
                                                           *working,
                                                           "selected snapshot",
                                                           whole ? RenderGraph::BufferRange {} :
                                                                   RenderGraph::BufferRange {17, 521});
            if (!captured)
            {
                return std::unexpected(captured.error());
            }
            auto compiled = graph.compileHeapsExpected();
            if (whole)
            {
                if (compiled)
                {
                    return std::unexpected("Whole snapshot read uninitialized neighboring bytes");
                }
                continue;
            }
            auto frame = Frame::create(device);
            if (!compiled || !frame)
            {
                return std::unexpected("Compile selected-only snapshot failed");
            }
            auto commands = (*frame)->beginExpected();
            if (!commands || !graph.executeExpected(*commands) || !(*frame)->submitAndWaitExpected())
            {
                return std::unexpected("Submit selected-only snapshot failed");
            }
            auto buffer = graph.getBufferExpected(*captured);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            auto data = readbackBufferExpected(device, **buffer);
            if (!data || data->bytes != std::vector<std::byte>(521, std::byte {0x93}))
            {
                return std::unexpected("Selected-only snapshot copied incorrect bytes");
            }
        }
        return {};
    }

    inline Result run(Device& device)
    {
        if (auto checked = validation(device); !checked)
        {
            return checked;
        }
        if (auto checked = selectedInitialization(device); !checked)
        {
            return checked;
        }
        uint64_t bytes = 521;
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            auto checked = snapshots(device, mode);
            if (!checked)
            {
                return std::unexpected("Snapshot mode " + std::to_string(mode) + ": " + checked.error());
            }
            bytes += *checked;
        }
        std::printf("GRAPH_BUFFER_CAPTURE exact_bytes=%llu plans=3 generations=9 selected_initialization=1 "
                    "discard_retry=1 explicit_queue_chain=1\n",
                    static_cast<unsigned long long>(bytes));
        return {};
    }
} // namespace vultra::test::graph_buffer_capture_probe
