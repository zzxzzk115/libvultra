#pragma once

#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace vultra::test::buffer_history_probe
{
    using Result              = std::expected<void, std::string>;
    using Inspect             = Result (*)(const RenderGraph&);
    constexpr uint64_t kBytes = 257;

    inline VriBufferDesc description()
    {
        return {kBytes,
                0,
                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                VriMemoryLocation_Device};
    }

    struct Faults
    {
        Device&               device;
        VriCoreInterface      original;
        VriDeviceDesc         caps;
        bool                  failMap        = false;
        bool                  failAllocation = false;
        bool                  disableRanges  = false;
        uint64_t              lastMapSize    = 0;
        inline static Faults* s_Active       = nullptr;

        explicit Faults(Device& gpu) :
            device(gpu),
            original(gpu.core),
            caps(*gpu.core.GetDeviceDesc(gpu.handle))
        {
            s_Active                  = this;
            device.core.MapBuffer     = map;
            device.core.CreateBuffer  = create;
            device.core.GetDeviceDesc = query;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            s_Active->lastMapSize = size;
            return s_Active->failMap ? nullptr : s_Active->original.MapBuffer(buffer, offset, size);
        }

        static VriResult VRI_CALL create(VriDevice* gpu, const VriBufferDesc* desc, VriBuffer** output)
        {
            if (s_Active->failAllocation)
            {
                *output = nullptr;
                return VriResult_Failure;
            }
            return s_Active->original.CreateBuffer(gpu, desc, output);
        }

        static const VriDeviceDesc* VRI_CALL query(const VriDevice* gpu)
        {
            if (s_Active->disableRanges && gpu == s_Active->device.handle)
            {
                s_Active->caps.hasBufferRangeBarriers = VRI_FALSE;
                return &s_Active->caps;
            }
            return s_Active->original.GetDeviceDesc(gpu);
        }
    };

    inline Result validation(Device& device)
    {
        RenderGraph graph(device);
        if (!graph.addPassExpected(
                "prefix",
                {},
                [](auto*, auto&) -> Result
                {
                    return {};
                },
                true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Compile buffer history validation prefix failed");
        }
        const auto before = graph.snapshotExpected();
        if (!before)
        {
            return std::unexpected(before.error());
        }
        for (unsigned attempt = 0; attempt < 7; ++attempt)
        {
            auto                       desc = description();
            std::array<std::byte, 3>   wrong {};
            std::span<const std::byte> initial;
            Faults                     fault(device);
            switch (attempt)
            {
                case 0:
                    desc.size = 0;
                    break;
                case 1:
                    desc.usage = VriBufferUsage_TransferSrc;
                    break;
                case 2:
                    desc.memoryLocation = VriMemoryLocation_Undefined;
                    break;
                case 3:
                    initial = wrong;
                    break;
                case 4:
                    desc.size = UINT64_MAX;
                    break;
                case 5:
                    fault.failMap = true;
                    break;
                case 6:
                    fault.failAllocation = true;
                    break;
            }
            if (graph.createHistoryBufferExpected("rejected", desc, initial))
            {
                return std::unexpected("Invalid/failed buffer history creation was published");
            }
            auto after = graph.snapshotExpected();
            if (!after || after->resources.size() != before->resources.size() ||
                after->passes.size() != before->passes.size() || !after->bufferHistories.empty())
            {
                return std::unexpected("Failed buffer history creation changed the compiled prefix");
            }
        }
        return {};
    }

    // Initial clear -> compute reader -> graphics writers; all resources/fences survive the final wait.
    inline Result multiQueue(RenderGraph& graph)
    {
        auto&      device = graph.device();
        VriFence*  raw    = nullptr;
        const auto status = device.core.CreateFence(device.handle, 0, &raw);
        std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(raw, device.core.DestroyFence);
        auto                                                                initial = Frame::create(device);
        auto reader  = Frame::create(device, VriQueueType_Compute);
        auto writers = Frame::create(device);
        if (status != VriResult_Success || !fence || !initial || !reader || !writers)
        {
            return std::unexpected("Create history queue-chain owners failed");
        }
        auto clearCommands = (*initial)->beginExpected();
        auto readCommands  = (*reader)->beginExpected();
        auto writeCommands = (*writers)->beginExpected();
        if (!clearCommands || !readCommands || !writeCommands)
        {
            return std::unexpected("Begin history queue-chain commands failed");
        }
        const std::array recordings {RenderGraph::PassRecording {"feedback", *readCommands},
                                     RenderGraph::PassRecording {"writer A", *writeCommands},
                                     RenderGraph::PassRecording {"writer B", *writeCommands}};
        const std::array order {RenderGraph::RecordingDependency {*clearCommands, *readCommands},
                                RenderGraph::RecordingDependency {*readCommands, *writeCommands}};
        auto missingClear = graph.executeOrderedExpected(*clearCommands, recordings, std::span(order).last(1));
        if (missingClear || missingClear.error().find("misses a required submission dependency") == std::string::npos)
        {
            return std::unexpected("Routed buffer history accepted a missing clear dependency");
        }
        if (auto recorded = graph.executeOrderedExpected(*clearCommands, recordings, order); !recorded)
        {
            return recorded;
        }
        const std::array initialized {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands}};
        const std::array consumed {VriFenceSubmitDesc {fence.get(), 2, VriPipelineStage_AllCommands}};
        if (!(*initial)->submitExpected({}, initialized) || !(*reader)->submitExpected(initialized, consumed) ||
            !(*writers)->submitExpected(consumed) || !(*writers)->waitExpected() || !(*reader)->waitExpected() ||
            !(*initial)->waitExpected())
        {
            return std::unexpected("Submit/complete history queue-chain failed");
        }
        return {};
    }

    inline std::expected<uint64_t, std::string> mixedHistories(Device& device)
    {
        auto uploadDesc           = description();
        uploadDesc.memoryLocation = VriMemoryLocation_HostUpload;
        auto upload               = Buffer::create(device, uploadDesc);
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        auto* mapped = device.core.MapBuffer((*upload)->handle, 0, kBytes);
        if (!mapped)
        {
            return std::unexpected("Map mixed history update input failed");
        }
        std::memset(mapped, 0x52, size_t(kBytes));
        device.core.UnmapBuffer((*upload)->handle);
        const std::vector<std::byte> initial(kBytes, std::byte {0x13});
        const std::array<float, 4>   initialColor {0.25f, 0.5f, 0.75f, 1};
        const std::array<float, 4>   changedColor {1, 0, 1, 0};
        VriClearValue                initialValue {};
        std::ranges::copy(initialColor, initialValue.color.f32);
        auto desc = colorTexture({1, 1}, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        RenderGraph graph(device);
        auto        input   = graph.importResourceExpected("update bytes", **upload);
        auto        buffer  = graph.createHistoryBufferExpected("mixed buffer", description(), initial);
        auto        texture = graph.createHistoryTextureExpected("mixed texture", desc, initialValue);
        if (!input || !buffer || !texture)
        {
            return std::unexpected("Declare mixed histories failed");
        }
        bool             write = true;
        const std::array uses {RenderGraph::Use {*input, Usage::eCopySource},
                               RenderGraph::Use {*buffer, Usage::eCopyDestination},
                               RenderGraph::Use {*texture, Usage::eStorageReadWrite}};
        if (!graph.addPassExpected("mixed mutation",
                                   uses,
                                   [&](auto* commands, auto& graph) -> Result
                                   {
                                       if (!write)
                                       {
                                           return {};
                                       }
                                       auto source = graph.getBufferExpected(*input);
                                       auto output = graph.getBufferExpected(*buffer);
                                       auto image  = graph.getTextureExpected(*texture);
                                       if (!source || !output || !image)
                                       {
                                           return std::unexpected("Resolve mixed history mutation outputs failed");
                                       }
                                       const VriBufferCopyDesc copy {0, 0, kBytes};
                                       device.core.CmdCopyBuffer(commands, (*output)->handle, (*source)->handle, &copy);
                                       VriClearColor value {};
                                       std::ranges::copy(changedColor, value.f32);
                                       return (*image)->clearStorageExpected(commands, value);
                                   }) ||
            !graph.exportResourceExpected(*buffer) || !graph.exportResourceExpected(*texture) ||
            !graph.compileExpected())
        {
            return std::unexpected("Compile mixed history mutation failed");
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        for (unsigned generation = 0; generation < 2; ++generation)
        {
            if (generation == 1)
            {
                const auto       epoch = graph.historyEpoch();
                const std::array rejected {RenderGraph::HistorySelection {*buffer, {}, {1, 13}},
                                           RenderGraph::HistorySelection {*texture, {1, 1, 0, 1}, {}}};
                if (graph.resetHistoryExpected(rejected) || graph.historyEpoch() != epoch)
                {
                    return std::unexpected("Invalid mixed selection was not rejected atomically");
                }
                auto retained = graph.bufferHistoryInfoExpected(*buffer, {1, 13});
                if (!retained || retained->resetPending || retained->epoch != epoch)
                {
                    return std::unexpected("Rejected texture selection changed earlier buffer history");
                }
                const std::array selected {rejected[0], RenderGraph::HistorySelection {*texture, {}, {}}};
                if (!graph.resetHistoryExpected(selected))
                {
                    return std::unexpected("Valid mixed history selection failed");
                }
                write = false;
            }
            auto commands = (*frame)->beginExpected();
            if (!commands || !graph.executeExpected(*commands) || !(*frame)->submitAndWaitExpected())
            {
                return std::unexpected("Submit mixed history generation failed");
            }
            auto output = graph.getBufferExpected(*buffer);
            auto image  = graph.getTextureExpected(*texture);
            if (!output || !image)
            {
                return std::unexpected("Resolve mixed history capture failed");
            }
            auto bytes    = readbackBufferExpected(device, **output);
            auto readback = TextureReadback::start(device, **image);
            if (!bytes || !readback)
            {
                return std::unexpected("Capture mixed history generation failed");
            }
            auto                   texels = (*readback)->dataExpected();
            std::vector<std::byte> expected(kBytes, std::byte {0x52});
            if (generation == 1)
            {
                std::fill_n(expected.begin() + 1, 13, std::byte {0x13});
            }
            const auto& color = generation ? initialColor : changedColor;
            if (bytes->bytes != expected || !texels || texels->bytes.size() != sizeof(color) ||
                std::memcmp(texels->bytes.data(), color.data(), sizeof(color)) != 0)
            {
                return std::unexpected("Mixed reset did not preserve selected/native buffer and color contents");
            }
        }
        return 2 * (kBytes + sizeof(initialColor));
    }

    inline Result repeatedZero(Device& device)
    {
        constexpr uint64_t                 chunk = 256 * 1024;
        constexpr uint64_t                 bytes = chunk * 2 + 37;
        constexpr RenderGraph::BufferRange selected {chunk - 11, chunk + 25};
        auto                               desc = description();
        desc.size                               = bytes;
        RenderGraph           graph(device);
        RenderGraph::Resource resource {};
        {
            Faults observed(device);
            auto   created = graph.createHistoryBufferExpected("large zero history", desc);
            if (!created || observed.lastMapSize != chunk)
            {
                return std::unexpected("Large zero history did not use a bounded upload snapshot");
            }
            resource = *created;
        }
        const std::array uses {RenderGraph::Use {resource, Usage::eCopySource}};
        if (!graph.addPassExpected(
                "large zero history consumer",
                uses,
                [](auto*, auto&) -> Result
                {
                    return {};
                },
                true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Compile large zero history failed");
        }
        const auto report  = graph.snapshotExpected();
        auto       history = graph.getBufferExpected(resource);
        auto       frame   = Frame::create(device);
        if (!report || report->bufferHistories.size() != 1 || report->bufferHistories[0].initialBytes != bytes ||
            !history || !frame)
        {
            return std::unexpected("Missing logical bytes or owners for large zero history");
        }
        const auto render = [&]() -> Result
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            return (*frame)->submitAndWaitExpected();
        };
        if (auto initialized = render(); !initialized)
        {
            return initialized;
        }
        auto initial = readbackBufferExpected(device, **history);
        if (!initial || initial->bytes.size() != bytes || std::ranges::count(initial->bytes, std::byte {0}) != bytes)
        {
            return std::unexpected("Repeated zero initialization missed history bytes");
        }

        constexpr std::byte canary {0xa7};
        auto upload = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        auto* mapped = device.core.MapBuffer((*upload)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map large history canary failed");
        }
        std::memset(mapped, std::to_integer<int>(canary), bytes);
        device.core.UnmapBuffer((*upload)->handle);
        (*upload)->setState({VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto ready =
                (*history)->transitionExpected(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !ready)
        {
            return ready;
        }
        const VriBufferCopyDesc copy {0, 0, bytes};
        device.core.CmdCopyBuffer(*commands, (*history)->handle, (*upload)->handle, &copy);
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }

        const std::array selection {RenderGraph::HistorySelection {resource, {}, selected}};
        if (auto reset = graph.resetHistoryExpected(selection); !reset)
        {
            return reset;
        }
        auto checkpoint = graph.checkpointExpected();
        commands        = (*frame)->beginExpected();
        if (!checkpoint || !commands || !graph.executeExpected(*commands) || !(*frame)->discardExpected() ||
            !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("Discarded large zero history lost its checkpoint");
        }
        const auto pending = graph.bufferHistoryInfoExpected(resource, selected);
        if (!pending || !pending->resetPending)
        {
            return std::unexpected("Discarded large zero history reset lost its pending interval");
        }
        if (auto retried = render(); !retried)
        {
            return retried;
        }
        auto actual = readbackBufferExpected(device, **history);
        if (!actual || actual->bytes.size() != bytes)
        {
            return std::unexpected("Read back large zero history after retry failed");
        }
        const auto begin = actual->bytes.begin() + selected.offset;
        const auto end   = begin + selected.size;
        if (!std::ranges::all_of(actual->bytes.begin(),
                                 begin,
                                 [](std::byte value)
                                 {
                                     return value == canary;
                                 }) ||
            !std::ranges::all_of(begin,
                                 end,
                                 [](std::byte value)
                                 {
                                     return value == std::byte {0};
                                 }) ||
            !std::ranges::all_of(end,
                                 actual->bytes.end(),
                                 [](std::byte value)
                                 {
                                     return value == canary;
                                 }))
        {
            return std::unexpected("Repeated zero reset changed adjacent history or missed its tail");
        }
        std::printf(
            "BUFFER_HISTORY_REPEATED_ZERO exact_bytes=%llu logical_bytes=%llu upload_bytes=%llu discard_retry=1\n",
            static_cast<unsigned long long>(2 * bytes),
            static_cast<unsigned long long>(bytes),
            static_cast<unsigned long long>(chunk));
        return {};
    }

    inline Result run(Device& device, Inspect inspect = nullptr)
    {
        if (auto checked = validation(device); !checked)
        {
            return checked;
        }
        if (auto checked = repeatedZero(device); !checked)
        {
            return checked;
        }
        auto mixed = mixedHistories(device);
        if (!mixed)
        {
            return std::unexpected(mixed.error());
        }
        std::array<std::vector<std::byte>, 3> initial;
        for (size_t item = 0; item < initial.size(); ++item)
        {
            initial[item].resize(kBytes);
            for (size_t byte = 0; byte < kBytes; ++byte)
            {
                initial[item][byte] = std::byte((byte * 13 + item * 53 + 7) & 255);
            }
        }
        auto uploadDesc           = description();
        uploadDesc.memoryLocation = VriMemoryLocation_HostUpload;
        auto uploadA              = Buffer::create(device, uploadDesc);
        auto uploadB              = Buffer::create(device, uploadDesc);
        auto replacement          = Buffer::create(device, uploadDesc);
        if (!uploadA || !uploadB || !replacement)
        {
            return std::unexpected("Create history update inputs failed");
        }
        const std::vector<std::byte> valueA(kBytes, std::byte {0x42});
        const std::vector<std::byte> valueB(kBytes, std::byte {0xa7});
        const std::vector<std::byte> replacementValue(kBytes, std::byte {0x73});
        for (const auto& pair : {std::pair {uploadA->get(), &valueA},
                                 std::pair {uploadB->get(), &valueB},
                                 std::pair {replacement->get(), &replacementValue}})
        {
            auto* mapped = device.core.MapBuffer(pair.first->handle, 0, kBytes);
            if (!mapped)
            {
                return std::unexpected("Map history update input failed");
            }
            std::memcpy(mapped, pair.second->data(), kBytes);
            device.core.UnmapBuffer(pair.first->handle);
        }
        RenderGraph graph(device);
        auto        a        = graph.createHistoryBufferExpected("history A", description(), initial[0]);
        auto        b        = graph.createHistoryBufferExpected("history B", description(), initial[1]);
        auto        feedback = graph.createHistoryBufferExpected("feedback history", description(), initial[2]);
        auto        zero     = graph.createHistoryBufferExpected("zero history", description());
        auto        inputA   = graph.importResourceExpected("input A", **uploadA);
        auto        inputB   = graph.importResourceExpected("input B", **uploadB);
        if (!a || !b || !feedback || !zero || !inputA || !inputB)
        {
            return std::unexpected("Declare buffer histories failed");
        }
        const std::array histories {*a, *b, *feedback, *zero};
        // Destroy/mutate the caller's initial bytes: reset must use the graph-owned immutable snapshots.
        const auto expectedInitial = initial;
        for (auto& bytes : initial)
        {
            std::ranges::fill(bytes, std::byte {0xff});
            bytes.clear();
        }
        bool       write   = true;
        bool       reject  = false;
        const auto addCopy = [&](std::string              name,
                                 RenderGraph::Resource    source,
                                 RenderGraph::Resource    destination,
                                 RenderGraph::BufferRange range,
                                 bool                     conditional) -> Result
        {
            const std::array uses {RenderGraph::Use {source, Usage::eCopySource, false, {}, range},
                                   RenderGraph::Use {destination, Usage::eCopyDestination, !conditional, {}, range}};
            return graph.addPassExpected(
                std::move(name),
                uses,
                [&, source, destination, range, conditional](auto* commands, auto& graph) -> Result
                {
                    if (conditional && reject)
                    {
                        return std::unexpected("Discard buffer history recording");
                    }
                    if (conditional && !write)
                    {
                        return {};
                    }
                    auto input  = graph.getBufferExpected(source);
                    auto output = graph.getBufferExpected(destination);
                    if (!input || !output)
                    {
                        return std::unexpected(input ? output.error() : input.error());
                    }
                    const VriBufferCopyDesc copy {range.offset, range.offset, range.size};
                    device.core.CmdCopyBuffer(commands, (*output)->handle, (*input)->handle, &copy);
                    return {};
                });
        };
        if (!addCopy("feedback", *a, *feedback, {13, 77}, false) || !addCopy("writer A", *inputA, *a, {13, 77}, true) ||
            !addCopy("writer B", *inputB, *b, {128, 80}, true))
        {
            return std::unexpected("Build cross-frame history feedback failed");
        }
        for (auto resource : histories)
        {
            if (auto exported = graph.exportResourceExpected(resource); !exported)
            {
                return exported;
            }
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        std::array<VriBuffer*, histories.size()> handles;
        for (size_t index = 0; index < histories.size(); ++index)
        {
            auto buffer = graph.getBufferExpected(histories[index]);
            auto info   = graph.resourceInfoExpected(histories[index]);
            if (!buffer || !info || !info->history || info->heap != UINT32_MAX)
            {
                return std::unexpected("Buffer history was aliased or lost its identity");
            }
            handles[index] = (*buffer)->handle;
        }
        std::array<std::vector<std::byte>, histories.size()> expected {expectedInitial[0],
                                                                       expectedInitial[1],
                                                                       expectedInitial[2],
                                                                       std::vector<std::byte>(kBytes)};
        uint64_t                                             checkedBytes = *mixed;
        const auto                                           verify       = [&]() -> Result
        {
            for (size_t index = 0; index < histories.size(); ++index)
            {
                auto buffer = graph.getBufferExpected(histories[index]);
                if (!buffer || (*buffer)->handle != handles[index])
                {
                    return std::unexpected("History reset changed buffer allocation identity");
                }
                auto bytes = readbackBufferExpected(device, **buffer);
                if (!bytes || bytes->bytes != expected[index])
                {
                    return std::unexpected("Buffer history bytes differ: " + std::to_string(index));
                }
                checkedBytes += bytes->bytes.size();
            }
            return inspect ? inspect(graph) : Result {};
        };
        const auto render = [&]() -> Result
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            return (*frame)->submitAndWaitExpected();
        };
        if (inspect)
        {
            if (auto checked = inspect(graph); !checked)
            {
                return checked;
            }
        }
        if (graph.historyInfoExpected(*a, {}) || graph.bufferHistoryInfoExpected(*inputA, {}) ||
            graph.bufferHistoryInfoExpected(*a, {kBytes, 0}))
        {
            return std::unexpected("History query accepted the wrong kind or range");
        }
        if (auto rendered = render(); !rendered)
        {
            return rendered;
        }
        std::copy_n(expectedInitial[0].begin() + 13, 77, expected[2].begin() + 13);
        std::copy_n(valueA.begin() + 13, 77, expected[0].begin() + 13);
        std::copy_n(valueB.begin() + 128, 80, expected[1].begin() + 128);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        write = false;
        if (auto rendered = render(); !rendered)
        {
            return rendered;
        }
        std::copy_n(expected[0].begin() + 13, 77, expected[2].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        const auto       oldEpoch = graph.historyEpoch();
        const std::array selected {RenderGraph::HistorySelection {*a, {}, {17, 19}},
                                   RenderGraph::HistorySelection {*a, {}, {23, 11}}};
        {
            Faults faults(device);
            faults.disableRanges = true;
            if (graph.resetHistoryExpected(selected) || graph.historyEpoch() != oldEpoch)
            {
                return std::unexpected("Unsupported partial history reset changed epochs");
            }
        }
        const std::array invalid {selected[0], RenderGraph::HistorySelection {*inputA, {}, {0, 1}}};
        const std::array wrongKind {RenderGraph::HistorySelection {*a, {1, 1, 0, 1}, {}}};
        if (graph.resetHistoryExpected(invalid) || graph.resetHistoryExpected(wrongKind) ||
            graph.historyEpoch() != oldEpoch || !graph.resetHistoryExpected(selected))
        {
            return std::unexpected("Atomic byte-range history selection failed");
        }
        auto reset       = graph.bufferHistoryInfoExpected(*a, {17, 19});
        auto neighbor    = graph.bufferHistoryInfoExpected(*a, {0, 13});
        auto independent = graph.bufferHistoryInfoExpected(*b, {});
        if (!reset || !neighbor || !independent || reset->epoch != oldEpoch + 1 || !reset->resetPending ||
            neighbor->epoch != oldEpoch || neighbor->resetPending || independent->epoch != oldEpoch)
        {
            return std::unexpected("Selected history reset changed neighboring or independent epochs");
        }
        if (inspect)
        {
            if (auto checked = inspect(graph); !checked)
            {
                return checked;
            }
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        reject      = true;
        auto failed = graph.executeExpected(*commands);
        reject      = false;
        if (failed || failed.error().find("Discard buffer history recording") == std::string::npos ||
            !(*frame)->discardExpected())
        {
            return std::unexpected("Buffer history callback failure did not discard");
        }
        if (auto retained = verify(); !retained)
        {
            return retained;
        }
        auto checkpoint = graph.checkpointExpected();
        commands        = (*frame)->beginExpected();
        if (!checkpoint || !commands || !graph.executeExpected(*commands) || !(*frame)->discardExpected() ||
            !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("Known-unsubmitted history reset restoration failed");
        }
        auto pending = graph.bufferHistoryInfoExpected(*a, {17, 19});
        if (!pending || !pending->resetPending)
        {
            return std::unexpected("Discarded history copy lost its pending byte ranges");
        }
        if (auto rendered = render(); !rendered)
        {
            return rendered;
        }
        std::copy_n(expectedInitial[0].begin() + 17, 19, expected[0].begin() + 17);
        std::copy_n(expected[0].begin() + 13, 77, expected[2].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        const auto       incomingEpoch = graph.historyEpoch();
        const std::array unusedEdit {RenderGraph::ResourceRange {*inputA, {}, {1, 12}}};
        const std::array edit {RenderGraph::ResourceRange {*inputA, {}, {29, 1}}};
        const std::array invalidEdit {edit[0], RenderGraph::ResourceRange {*inputB, {}, {kBytes, 0}}};
        if (!graph.resetDependentHistoryExpected({}, unusedEdit) ||
            graph.resetDependentHistoryExpected({}, invalidEdit) || graph.historyEpoch() != incomingEpoch)
        {
            return std::unexpected("Unused or invalid incoming byte edit changed history");
        }
        if (auto edited = graph.resetDependentHistoryExpected({}, edit); !edited)
        {
            return edited;
        }
        auto affected   = graph.bufferHistoryInfoExpected(*feedback, {13, 77});
        auto unaffected = graph.bufferHistoryInfoExpected(*feedback, {0, 13});
        independent     = graph.bufferHistoryInfoExpected(*b, {});
        if (!affected || !unaffected || !independent || !affected->resetPending ||
            affected->epoch != graph.historyEpoch() || unaffected->epoch != oldEpoch || independent->epoch != oldEpoch)
        {
            return std::unexpected("Incoming byte edit did not invalidate selected cross-frame feedback only");
        }
        if (auto rendered = multiQueue(graph); !rendered)
        {
            return rendered;
        }
        std::copy_n(expectedInitial[0].begin() + 13, 77, expected[0].begin() + 13);
        std::copy_n(expected[0].begin() + 13, 77, expected[2].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        write = true;
        if (auto rendered = render(); !rendered)
        {
            return rendered;
        }
        std::copy_n(valueA.begin() + 13, 77, expected[0].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }

        const auto       beforeRebind = graph.historyEpoch();
        const std::array batch {RenderGraph::ImportBinding {*inputA, **replacement},
                                RenderGraph::ImportBinding {*inputB, **uploadB}};
        auto             checkpointBeforeRejection = graph.checkpointExpected();
        if (!checkpointBeforeRejection)
        {
            return std::unexpected(checkpointBeforeRejection.error());
        }
        {
            Faults faults(device);
            faults.disableRanges = true;
            auto rejected        = graph.bindAndResetDependentHistoryExpected(batch);
            if (rejected || !rejected.error().contains("requires native byte-range barriers") ||
                graph.getBufferExpected(*inputA) != uploadA->get() ||
                graph.getBufferExpected(*inputB) != uploadB->get() || graph.historyEpoch() != beforeRebind ||
                graph.resourceInfo(*a).historyResetPending || graph.resourceInfo(*feedback).historyResetPending)
            {
                return std::unexpected("Failed dependent history reset partially published an import rebind");
            }
        }
        if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpointBeforeRejection)); !restored)
        {
            return restored;
        }
        auto stale = graph.checkpointExpected();
        if (!stale)
        {
            return std::unexpected(stale.error());
        }
        auto rebound = graph.bindAndResetDependentHistoryExpected(batch);
        if (!rebound || !*rebound)
        {
            return std::unexpected(rebound ? "Changed import batch reported no replacement" : rebound.error());
        }
        affected    = graph.bufferHistoryInfoExpected(*feedback, {13, 77});
        unaffected  = graph.bufferHistoryInfoExpected(*feedback, {0, 13});
        independent = graph.bufferHistoryInfoExpected(*b, {});
        if (graph.getBufferExpected(*inputA) != replacement->get() || graph.historyEpoch() != beforeRebind + 1 ||
            !affected || !affected->resetPending || affected->epoch != graph.historyEpoch() || !unaffected ||
            unaffected->epoch != oldEpoch || unaffected->resetPending || !independent ||
            independent->epoch != oldEpoch || independent->resetPending ||
            graph.restoreUnsubmittedExpected(std::move(*stale)))
        {
            return std::unexpected("Successful import rebind did not reset only dependent history");
        }
        if (auto rendered = render(); !rendered)
        {
            return rendered;
        }
        std::copy_n(replacementValue.begin() + 13, 77, expected[0].begin() + 13);
        std::copy_n(expectedInitial[0].begin() + 13, 77, expected[2].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        auto stable = graph.checkpointExpected();
        auto identity = graph.bindAndResetDependentHistoryExpected(batch);
        if (!stable || !identity || *identity || graph.historyEpoch() != beforeRebind + 1 ||
            graph.resourceInfo(*a).historyResetPending || !graph.restoreUnsubmittedExpected(std::move(*stable)))
        {
            return std::unexpected("An identical import batch invalidated history or a checkpoint");
        }
        std::puts("IMPORT_HISTORY_REBIND rejected_atomically=1 no_op=1 feedback=1 replacement_bytes=77");

        write = false;
        if (!graph.resetHistoryExpected() || !render())
        {
            return std::unexpected("Global buffer history reset failed");
        }
        expected = {expectedInitial[0], expectedInitial[1], expectedInitial[2], std::vector<std::byte>(kBytes)};
        std::copy_n(expectedInitial[0].begin() + 13, 77, expected[2].begin() + 13);
        if (auto checked = verify(); !checked)
        {
            return checked;
        }
        std::puts("IMPORT_HISTORY_BUFFER unaligned_byte=1 unused_range=1 atomic=1 feedback=1 explicit_queue_chain=1");
        std::printf("BUFFER_HISTORY exact_bytes=%llu frames=9 selected_epochs=1 feedback=1 discard_retry=1 "
                    "explicit_queue_chain=1\n",
                    static_cast<unsigned long long>(checkedBytes));
        return {};
    }
} // namespace vultra::test::buffer_history_probe
