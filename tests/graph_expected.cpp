#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vultra;
    VriResult (*createTexture)(VriDevice*, const VriTextureDesc*, VriTexture**) = nullptr;
    unsigned textureCreates                                                     = 0;

    VriResult VRI_CALL rejectAllocatorReset(VriCommandAllocator*)
    {
        return VriResult_OutOfMemory;
    }

    VriResult VRI_CALL rejectQueueSubmission(VriQueue*, const VriQueueSubmitDesc*)
    {
        return VriResult_OutOfMemory;
    }

    VriResult VRI_CALL loseDeviceOnWait(VriFence*, uint64_t, uint64_t)
    {
        return VriResult_DeviceLost;
    }

    std::expected<void, std::string> verifyCheckedFrameReset(Device& gpu)
    {
        if (!gpu.core.ResetCommandAllocatorChecked)
        {
            return {};
        }
        auto        frame = Frame::create(gpu);
        RenderGraph graph(gpu);
        if (!frame || !graph.compileExpected())
        {
            return std::unexpected("Create checked frame checkpoint fixture failed");
        }
        const std::array<const Frame*, 1> frames {frame->get()};
        if (graph.checkpointExpected(frames))
        {
            return std::unexpected("Graph accepted an idle frame in a checked checkpoint");
        }
        const auto reset  = std::exchange(gpu.core.ResetCommandAllocatorChecked, rejectAllocatorReset);
        auto       failed = (*frame)->beginExpected();
        gpu.core.ResetCommandAllocatorChecked = reset;
        if (failed || failed.error().find("Reset command allocator failed") == std::string::npos ||
            !(*frame)->beginExpected())
        {
            return std::unexpected("Frame did not report and recover from a begin reset rejection");
        }
        auto checkpoint = graph.checkpointExpected(frames);
        if (!checkpoint || !(*frame)->discardExpected() || !(*frame)->beginExpected() || !(*frame)->discardExpected() ||
            graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("A later discard proved an older recording's checkpoint");
        }
        if (!(*frame)->beginExpected())
        {
            return std::unexpected("Begin device-loss completion fixture failed");
        }
        auto submitted = graph.checkpointExpected(frames);
        if (!submitted || !(*frame)->submitExpected())
        {
            return std::unexpected("Submit device-loss completion fixture failed");
        }
        const auto nativeWait = std::exchange(gpu.core.WaitForFence, loseDeviceOnWait);
        const auto lost       = (*frame)->waitExpected();
        gpu.core.WaitForFence = nativeWait;
        if (lost || (*frame)->discardExpected() || (*frame)->beginExpected() ||
            graph.restoreUnsubmittedExpected(std::move(*submitted)))
        {
            return std::unexpected("Device-loss completion allowed recording reuse or rollback");
        }
        std::puts("GRAPH_CHECKED_FRAME_RESET begin_retry=1 stale_discard_rejected=1 lost_wait_terminal=1");
        return {};
    }

    std::expected<void, std::string> verifyGuardedCheckpointOrder(Device& gpu)
    {
        if (!gpu.core.ResetCommandAllocatorChecked)
        {
            return {};
        }
        auto        first  = Frame::create(gpu);
        auto        second = Frame::create(gpu);
        RenderGraph graph(gpu);
        if (!first || !second ||
            !graph.addPassExpected(
                "track recording destination",
                {},
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Create guarded checkpoint ordering fixture failed");
        }
        auto*                             a = (*first)->recordingDestination();
        auto*                             b = (*second)->recordingDestination();
        const std::array<const Frame*, 2> frames {first->get(), second->get()};
        const auto                        begin = [&]
        {
            return (*first)->beginExpected() && (*second)->beginExpected();
        };
        if (!begin())
        {
            return std::unexpected("Begin nested checkpoint recordings failed");
        }
        auto older = graph.checkpointExpected(std::span(frames).first(1));
        auto newer = graph.checkpointExpected(std::span(frames).last(1));
        if (!older || !newer || !graph.executeExpected(b) || !(*second)->discardExpected())
        {
            return std::unexpected("Record and discard the later checkpoint commands failed");
        }
        const auto uncovered = graph.validateCheckpointRestoreExpected(*older);
        if (uncovered || !uncovered.error().contains("omits a recorded command buffer") ||
            !graph.restoreUnsubmittedExpected(std::move(*newer)) || !(*first)->discardExpected() ||
            !graph.restoreUnsubmittedExpected(std::move(*older)))
        {
            return std::unexpected("A proved later rollback did not remove its recording from the earlier checkpoint");
        }

        if (!begin())
        {
            return std::unexpected("Begin superseded checkpoint recordings failed");
        }
        older = graph.checkpointExpected(frames);
        if (!older || !graph.executeExpected(a))
        {
            return std::unexpected("Record the earlier commands for a superseded checkpoint failed");
        }
        newer = graph.checkpointExpected(std::span(frames).last(1));
        if (!newer || !graph.executeExpected(b) || !(*second)->discardExpected() || !(*first)->discardExpected() ||
            !graph.restoreUnsubmittedExpected(std::move(*older)) || graph.validateCheckpointRestoreExpected(*newer))
        {
            return std::unexpected("An earlier rollback left a dependent later checkpoint usable");
        }
        std::puts("GRAPH_GUARD_DESTINATIONS omitted=1 nested_restore=1 superseded=1");
        return {};
    }

    std::expected<void, std::string> verifyPassState(Device& gpu)
    {
        std::array<unsigned, 2> counters {11, 22};
        unsigned                calls           = 0;
        unsigned                captures        = 0;
        unsigned                order           = 0;
        unsigned                captureFailure  = 0;
        bool                    fail            = true;
        bool                    recoveryFailure = false;
        const auto              capture = [&](size_t index) -> std::expected<RenderGraph::RestorePassState, std::string>
        {
            ++captures;
            if (captureFailure == 1)
            {
                return std::unexpected("injected CPU snapshot failure");
            }
            if (captureFailure == 2)
            {
                return RenderGraph::RestorePassState {};
            }
            return RenderGraph::RestorePassState {
                [&, index, value = counters[index]]() noexcept -> std::expected<void, std::string>
                {
                    if (recoveryFailure && index == 1)
                    {
                        return std::unexpected("injected restoration failure");
                    }
                    counters[index] = value;
                    order           = order * 10 + unsigned(index + 1);
                    return {};
                }};
        };
        RenderGraph graph(gpu);
        if (!graph.addPassExpected(
                "counter A",
                {},
                [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    ++calls;
                    ++counters[0];
                    return {};
                },
                true,
                [&]
                {
                    return capture(0);
                }) ||
            !graph.addPassExpected(
                "counter B",
                {},
                [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    ++calls;
                    counters[1] += 10;
                    if (fail)
                    {
                        return std::unexpected("injected CPU-state recording failure");
                    }
                    return {};
                },
                true,
                [&]
                {
                    return capture(1);
                }) ||
            !graph.addPassExpected(
                "culled state",
                {},
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return std::unexpected("Culled stateful pass executed");
                },
                false,
                []() -> std::expected<RenderGraph::RestorePassState, std::string>
                {
                    return std::unexpected("Culled CPU state was captured");
                }) ||
            !graph.compileExpected())
        {
            return std::unexpected("Prepare CPU-state graph failed");
        }
        auto first  = Frame::create(gpu);
        auto second = Frame::create(gpu);
        if (!first || !second)
        {
            return std::unexpected("Create CPU-state frames failed");
        }
        for (unsigned failure : {1u, 2u})
        {
            captureFailure = failure;
            auto commands  = (*first)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            const auto rejected = graph.executeExpected(*commands);
            if (rejected || calls != 0 || counters != std::array {11u, 22u} || order != 0 ||
                !(*first)->discardExpected())
            {
                return std::unexpected("Invalid CPU snapshot did not reject before recording");
            }
        }
        captureFailure = 0;
        auto a         = (*first)->beginExpected();
        auto b         = (*second)->beginExpected();
        if (!a || !b)
        {
            return std::unexpected("Begin routed CPU-state frames failed");
        }
        const std::array destinations {RenderGraph::PassRecording {"counter A", *a},
                                       RenderGraph::PassRecording {"counter B", *b}};
        auto             failed = graph.executeExpected(*a, destinations);
        if (failed || failed.error().find("injected CPU-state") == std::string::npos ||
            counters != std::array {11u, 22u} || order != 21 || calls != 2 || !(*first)->discardExpected() ||
            !(*second)->discardExpected())
        {
            return std::unexpected("Routed failure did not restore CPU snapshots in reverse pass order");
        }
        fail            = false;
        order           = 0;
        auto checkpoint = graph.checkpointExpected();
        a               = (*first)->beginExpected();
        if (!checkpoint || !a || !graph.executeExpected(*a) || counters != std::array {12u, 32u} || order != 0 ||
            !(*first)->discardExpected())
        {
            return std::unexpected("Prepare successful but discarded CPU recording failed");
        }
        RenderGraph::StateCheckpoint moved;
        moved = std::move(*checkpoint);
        if (!graph.restoreUnsubmittedExpected(std::move(moved)) || counters != std::array {11u, 22u} || order != 21)
        {
            return std::unexpected("Moved CPU checkpoint did not restore its state");
        }
        order = 0;
        a     = (*first)->beginExpected();
        if (!a || !graph.executeExpected(*a) || !(*first)->submitAndWaitExpected() ||
            counters != std::array {12u, 32u} || order != 0)
        {
            return std::unexpected("Completed recording unexpectedly restored speculative CPU state");
        }
        checkpoint = graph.checkpointExpected();
        if (!checkpoint ||
            !graph.addPassExpected(
                "late",
                {},
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true) ||
            !graph.compileExpected() || graph.restoreUnsubmittedExpected(std::move(*checkpoint)) ||
            counters != std::array {12u, 32u} || order != 0)
        {
            return std::unexpected("Edited pass topology accepted a stale CPU checkpoint");
        }
        recoveryFailure = true;
        fail            = true;
        order           = 0;
        a               = (*first)->beginExpected();
        if (!a)
        {
            return std::unexpected(a.error());
        }
        auto broken = graph.executeExpected(*a);
        if (broken || broken.error().find("counter B: injected restoration failure") == std::string::npos ||
            broken.error().find("injected CPU-state recording failure") == std::string::npos || order != 1 ||
            counters[0] != 12 || !(*first)->discardExpected() || graph.checkpointExpected() ||
            graph.compileExpected() || graph.compileHeapsExpected())
        {
            return std::unexpected("Failed CPU restoration did not report the named pass and retire its graph");
        }
        a = (*first)->beginExpected();
        if (!a || graph.executeExpected(*a) || !(*first)->discardExpected())
        {
            return std::unexpected("Graph executed again after incomplete CPU restoration");
        }
        std::puts("PASS_STATE_RECOVERY_FAILURE named_error=passed remaining_restores=attempted graph_reuse=rejected");
        std::printf("Pass CPU snapshots: preflight, reverse restore, routed failure, discard and completion passed (%u "
                    "captures)\n",
                    captures);
        return {};
    }

    std::expected<void, std::string> verifyStateTransaction(Device& gpu)
    {
        constexpr std::array<uint32_t, 4> inputValues {3, 7, 11, 19};
        constexpr uint64_t                bytes = sizeof(inputValues);
        const VriBufferDesc               uploadDesc {bytes,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                        VriMemoryLocation_HostUpload};
        auto                              source      = Buffer::create(gpu, uploadDesc);
        auto                              replacement = Buffer::create(gpu, uploadDesc);
        auto                              output      = Buffer::create(
            gpu,
            {bytes, 0, VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostReadback});
        if (!source || !replacement || !output)
        {
            return std::unexpected("Create transaction fixture buffers failed");
        }
        auto* mapped = gpu.core.MapBuffer((*source)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map transaction input failed");
        }
        std::memcpy(mapped, inputValues.data(), bytes);
        gpu.core.UnmapBuffer((*source)->handle);
        const auto originalSource = (*source)->captureState();
        const auto originalOutput = (*output)->captureState();

        PassCatalog    catalog(gpu);
        size_t         factoryCalls = 0;
        PassDefinition definition;
        definition.type           = "transaction.factory";
        definition.createExpected = [&](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
        {
            ++factoryCalls;
            return std::unexpected("Factory was called inside a graph state transaction");
        };
        if (auto added = catalog.addExpected(std::move(definition)); !added)
        {
            return added;
        }

        RenderGraph graph(gpu);
        auto        input    = graph.importResourceExpected("upload", **source);
        auto        readback = graph.importResourceExpected("output", **output, false);
        auto        history  = graph.createHistoryBufferExpected(
            "history",
            {bytes,
                     0,
                     VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                     VriMemoryLocation_Device});
        if (!input || !readback || !history)
        {
            return std::unexpected("Declare transaction fixture resources failed");
        }
        GraphDefinition description;
        description.outputs = {"upload.buffer"};
        const std::array imports {GraphBinding {"upload.buffer", *input}};
        const auto       noop = [](auto*, auto&) -> std::expected<void, std::string>
        {
            return {};
        };
        const auto rejects = [](const auto& result)
        {
            return !result && result.error().find("active state transaction") != std::string::npos;
        };
        const auto epoch = graph.historyEpoch();
        const auto check = [&](VriCommandBuffer* commands) -> std::expected<void, std::string>
        {
            const std::array changed {std::string_view("write history")};
            const auto       found = graph.findResourceExpected("upload");
            if (!found || !rejects(graph.bindExpected(*input, **replacement)) ||
                !rejects(graph.importResourceExpected("extra upload", **replacement)) ||
                !rejects(graph.createBufferExpected("extra buffer", uploadDesc)) ||
                !rejects(graph.createHistoryBufferExpected(
                    "extra history",
                    {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_Device})) ||
                !rejects(graph.addPassExpected("extra pass", {}, noop, true)) ||
                !rejects(graph.nameResourceExpected(*input, "renamed")) ||
                !rejects(graph.exportResourceExpected(*history)) ||
                !rejects(graph.captureRangeAfterPassExpected("write history",
                                                             {*history, {}, {0, bytes}},
                                                             "extra snapshot")) ||
                !rejects(graph.addOrderingExpected("independent", "read history")) ||
                !rejects(graph.compileExpected()) || !rejects(graph.compileHeapsExpected()) ||
                !rejects(graph.resetHistoryExpected()) ||
                !rejects(graph.resetHistoryExpected(std::span(&*history, 1))) ||
                !rejects(graph.resetDependentHistoryExpected(changed)) || !rejects(graph.checkpointExpected()) ||
                !rejects(graph.restoreUnsubmittedExpected(RenderGraph::StateCheckpoint {})) ||
                !rejects(graph.executeExpected(commands)) ||
                !rejects(catalog.buildExpected(graph, "transaction.factory", "blocked", {})) ||
                !rejects(description.buildExpected(graph, catalog, imports)) ||
                graph.getBufferExpected(*input) != source->get() || found->index != input->index ||
                graph.historyEpoch() != epoch || !graph.snapshotExpected())
            {
                return std::unexpected("Graph accepted a callback edit or lost read-only access");
            }
            return {};
        };

        size_t           captures   = 0;
        size_t           executions = 0;
        size_t           restores   = 0;
        bool             fail       = true;
        const std::array write {RenderGraph::Use {*input, Usage::eCopySource},
                                RenderGraph::Use {*history, Usage::eCopyDestination, true}};
        const std::array read {RenderGraph::Use {*history, Usage::eCopySource},
                               RenderGraph::Use {*readback, Usage::eCopyDestination, true}};
        if (!graph.addPassExpected("independent", {}, noop, true) ||
            !graph.addPassExpected(
                "write history",
                write,
                [&](auto* commands, auto& active) -> std::expected<void, std::string>
                {
                    if (auto guarded = check(commands); !guarded)
                    {
                        return guarded;
                    }
                    ++executions;
                    auto target = active.getBufferExpected(*history);
                    if (!target)
                    {
                        return std::unexpected(target.error());
                    }
                    const VriBufferCopyDesc copy {0, 0, bytes};
                    gpu.core.CmdCopyBuffer(commands, (*target)->handle, (*source)->handle, &copy);
                    return {};
                },
                false,
                [&]() -> std::expected<RenderGraph::RestorePassState, std::string>
                {
                    if (auto guarded = check(nullptr); !guarded)
                    {
                        return std::unexpected(guarded.error());
                    }
                    ++captures;
                    return RenderGraph::RestorePassState {[&]() -> std::expected<void, std::string>
                                                          {
                                                              ++restores;
                                                              return check(nullptr);
                                                          }};
                }) ||
            !graph.addPassExpected("read history",
                                   read,
                                   [&](auto* commands, auto& active) -> std::expected<void, std::string>
                                   {
                                       if (fail)
                                       {
                                           return active.resetHistoryExpected();
                                       }
                                       auto previous = active.getBufferExpected(*history);
                                       if (!previous)
                                       {
                                           return std::unexpected(previous.error());
                                       }
                                       const VriBufferCopyDesc copy {0, 0, bytes};
                                       gpu.core.CmdCopyBuffer(commands, (*output)->handle, (*previous)->handle, &copy);
                                       return {};
                                   }) ||
            !graph.exportResourceExpected(*readback) || !graph.compileExpected())
        {
            return std::unexpected("Compile graph state transaction fixture failed");
        }
        auto current = graph.checkpointExpected();
        if (!current || !graph.compileExpected() || !graph.restoreUnsubmittedExpected(std::move(*current)))
        {
            return std::unexpected("Recompiling an unchanged graph invalidated a checkpoint");
        }
        auto stale = graph.checkpointExpected();
        if (!stale || !graph.addOrderingExpected("independent", "read history") || !graph.compileExpected() ||
            graph.restoreUnsubmittedExpected(std::move(*stale)) || restores != 1)
        {
            return std::unexpected("An order-only recompile accepted a stale checkpoint");
        }

        auto initialHistory = graph.getBufferExpected(*history);
        if (!initialHistory)
        {
            return std::unexpected(initialHistory.error());
        }
        const auto originalHistory = (*initialHistory)->captureState();
        VriFence*  raw             = nullptr;
        const auto created         = gpu.core.CreateFence(gpu.handle, 0, &raw);
        const std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(raw, gpu.core.DestroyFence);
        auto                                                                      graphics = Frame::create(gpu);
        auto compute = Frame::create(gpu, VriQueueType_Compute);
        if (created != VriResult_Success || !fence || !graphics || !compute)
        {
            return std::unexpected("Create transaction fixture recording destinations failed");
        }
        for (const bool injectFailure : {true, false})
        {
            fail   = injectFailure;
            auto a = (*graphics)->beginExpected();
            auto b = (*compute)->beginExpected();
            if (!a || !b)
            {
                return std::unexpected("Begin transaction fixture failed");
            }
            const std::array recordings {RenderGraph::PassRecording {"independent", *a},
                                         RenderGraph::PassRecording {"write history", *a},
                                         RenderGraph::PassRecording {"read history", *b}};
            const std::array order {RenderGraph::RecordingDependency {*a, *b}};
            const auto       recorded = graph.executeOrderedExpected(*a, recordings, order);
            if (fail)
            {
                const auto pending = graph.bufferHistoryInfoExpected(*history, {});
                if (!rejects(recorded) || recorded.error().find("read history") == std::string::npos || !pending ||
                    !pending->resetPending || (*source)->captureState() != originalSource ||
                    (*output)->captureState() != originalOutput ||
                    (*initialHistory)->captureState() != originalHistory || !(*graphics)->discardExpected() ||
                    !(*compute)->discardExpected())
                {
                    return std::unexpected("Rejected callback edit did not roll back the routed frame");
                }
            }
            else if (!recorded)
            {
                return recorded;
            }
        }
        const std::array dependency {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands}};
        if (!(*graphics)->submitExpected({}, dependency) || !(*compute)->submitExpected(dependency) ||
            !(*compute)->waitExpected() || !(*graphics)->waitExpected())
        {
            return std::unexpected("Submit transaction fixture retry failed");
        }
        const auto* data = gpu.core.MapBuffer((*output)->handle, 0, bytes);
        if (!data)
        {
            return std::unexpected("Map transaction readback failed");
        }
        const bool exact = std::memcmp(data, inputValues.data(), bytes) == 0;
        gpu.core.UnmapBuffer((*output)->handle);
        const auto completed = graph.bufferHistoryInfoExpected(*history, {});
        if (!exact || !completed || completed->resetPending || captures != 4 || executions != 2 || restores != 2 ||
            factoryCalls)
        {
            return std::unexpected("Transaction fixture lost retry output, callback order or history");
        }
        std::puts("GRAPH_STATE_TRANSACTION capture=1 record=1 restore=1 order_checkpoint=1 exact_routed_readback=1");
        return {};
    }

    std::expected<void, std::string> verifyAspectDependencies(Device& gpu)
    {
        auto desc  = colorTexture({8, 8}, VriFormat_D32_SFLOAT_S8_UINT);
        desc.usage = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        const RenderGraph::TextureRange depth {.aspect = VriImageAspect_Depth};
        const RenderGraph::TextureRange stencil {.aspect = VriImageAspect_Stencil};
        const auto                      noop = [](auto*, auto&) -> std::expected<void, std::string>
        {
            return {};
        };
        RenderGraph missing(gpu);
        auto        image = missing.createTextureExpected("missing stencil", desc);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        const std::array depthWrite {RenderGraph::Use {*image, Usage::eCopyDestination, true, depth}};
        const std::array stencilRead {RenderGraph::Use {*image, Usage::eCopySource, false, stencil}};
        if (!missing.addPassExpected("depth producer", depthWrite, noop) ||
            !missing.addPassExpected("stencil consumer", stencilRead, noop, true) || missing.compileExpected())
        {
            return std::unexpected("A depth producer initialized the unwritten stencil plane");
        }
        RenderGraph exported(gpu);
        auto        partial = exported.createTextureExpected("partial export", desc);
        if (!partial)
        {
            return std::unexpected(partial.error());
        }
        const std::array partialWrite {RenderGraph::Use {*partial, Usage::eCopyDestination, true, depth}};
        if (!exported.addPassExpected("depth only", partialWrite, noop) || !exported.exportResourceExpected(*partial) ||
            exported.compileExpected())
        {
            return std::unexpected("A whole texture export accepted unwritten stencil");
        }
        RenderGraph disjoint(gpu);
        auto        complete = disjoint.createTextureExpected("disjoint aspects", desc);
        if (!complete)
        {
            return std::unexpected(complete.error());
        }
        const std::array separate {RenderGraph::Use {*complete, Usage::eCopyDestination, true, depth},
                                   RenderGraph::Use {*complete, Usage::eCopyDestination, true, stencil}};
        if (!disjoint.addPassExpected("separate writers", separate, noop) ||
            !disjoint.exportResourceExpected(*complete) || !disjoint.compileExpected())
        {
            return std::unexpected("Disjoint aspect writes did not initialize the complete texture");
        }
        for (const auto aspect :
             std::array<VriImageAspectFlags, 2> {VriImageAspect_Color, VriImageAspect_Depth | VriImageAspect_Stencil})
        {
            RenderGraph overlap(gpu);
            auto        texture = overlap.createTextureExpected("invalid aspect uses", desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const RenderGraph::TextureRange selected {.aspect = VriImageAspectFlags(aspect)};
            const std::array                uses {RenderGraph::Use {*texture, Usage::eCopyDestination, true, selected},
                                   RenderGraph::Use {*texture, Usage::eCopyDestination, true, depth}};
            if (!overlap.addPassExpected("invalid", uses, noop, true) || overlap.compileExpected())
            {
                return std::unexpected("Invalid/overlapping aspect selection was accepted");
            }
        }
        RenderGraph buffers(gpu);
        auto        buffer =
            buffers.createBufferExpected("buffer", {16, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device});
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        const std::array invalidBuffer {RenderGraph::Use {*buffer, Usage::eStorageWrite, true, depth}};
        if (!buffers.addPassExpected("buffer aspects", invalidBuffer, noop, true) || buffers.compileExpected())
        {
            return std::unexpected("Buffer accepted a texture aspect selection");
        }
        RenderGraph unsupported(gpu);
        auto        unsupportedImage = unsupported.createTextureExpected("unsupported", desc);
        if (!unsupportedImage)
        {
            return std::unexpected(unsupportedImage.error());
        }
        const std::array use {RenderGraph::Use {*unsupportedImage, Usage::eCopyDestination, true, depth}};
        if (!unsupported.addPassExpected("independent plane", use, noop, true))
        {
            return std::unexpected("Build unavailable-capability control failed");
        }
        static VriDeviceDesc disabled;
        disabled                                = *gpu.core.GetDeviceDesc(gpu.handle);
        disabled.hasSeparateDepthStencilLayouts = VRI_FALSE;
        const auto query                        = gpu.core.GetDeviceDesc;
        gpu.core.GetDeviceDesc                  = [](const VriDevice*) -> const VriDeviceDesc*
        {
            return &disabled;
        };
        const auto rejected    = unsupported.compileExpected();
        gpu.core.GetDeviceDesc = query;
        if (rejected || unsupported.getTextureExpected(*unsupportedImage))
        {
            return std::unexpected("Unavailable independent layout allocated a texture or compiled");
        }
        std::puts("Aspect initialization, complete exports, disjoint uses and unsupported capability checks passed");
        return {};
    }

    std::expected<void, std::string> verifyRecordingDestinations(Device& gpu)
    {
        RenderGraph graph(gpu);
        auto        texture = graph.createTextureExpected("routed", colorTexture({8, 8}));
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        unsigned         calls = 0;
        bool             fail  = true;
        const std::array write {RenderGraph::Use {*texture, Usage::eColorWrite, true}};
        const std::array read {RenderGraph::Use {*texture, Usage::eSampled}};
        if (!graph.addPassExpected("write",
                                   write,
                                   [&](auto*, auto&) -> std::expected<void, std::string>
                                   {
                                       ++calls;
                                       return {};
                                   }) ||
            !graph.addPassExpected(
                "read",
                read,
                [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    ++calls;
                    if (fail)
                    {
                        return std::unexpected("injected routed failure");
                    }
                    return {};
                },
                true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Create routed graph fixture failed");
        }
        auto image  = graph.getTextureExpected(*texture);
        auto first  = Frame::create(gpu);
        auto second = Frame::create(gpu, VriQueueType_Compute);
        if (!image || !first || !second)
        {
            return std::unexpected("Create routed recording frames failed");
        }
        const auto original = (*image)->captureState();
        auto       a        = (*first)->beginExpected();
        auto       b        = (*second)->beginExpected();
        if (!a || !b)
        {
            return std::unexpected("Begin routed recording frames failed");
        }
        std::array destinations {RenderGraph::PassRecording {"write", *a}, RenderGraph::PassRecording {"read", *b}};
        if (graph.executeExpected(*a, std::span(destinations).first(1)))
        {
            return std::unexpected("Routed recording accepted a missing pass");
        }
        destinations[1].commands = nullptr;
        if (graph.executeExpected(*a, destinations))
        {
            return std::unexpected("Routed recording accepted a null destination");
        }
        destinations[1].commands = *b;
        std::swap(destinations[0], destinations[1]);
        if (graph.executeExpected(*a, destinations))
        {
            return std::unexpected("Routed recording accepted reordered passes");
        }
        std::swap(destinations[0], destinations[1]);
        if (calls || (*image)->captureState() != original)
        {
            return std::unexpected("Routed preflight rejection mutated callbacks or texture states");
        }
        auto failed = graph.executeExpected(*a, destinations);
        if (failed || failed.error().find("read: injected routed failure") == std::string::npos || calls != 2 ||
            (*image)->captureState() != original)
        {
            return std::unexpected("Multi-buffer recording failure did not restore the complete state");
        }
        if (!(*first)->discardExpected() || !(*second)->discardExpected())
        {
            return std::unexpected("Discard failed routed recording failed");
        }
        a = (*first)->beginExpected();
        b = (*second)->beginExpected();
        if (!a || !b)
        {
            return std::unexpected("Begin routed retry failed");
        }
        destinations[0].commands = *a;
        destinations[1].commands = *b;
        fail                     = false;
        auto checkpoint          = graph.checkpointExpected();
        if (!checkpoint || !graph.executeExpected(*a, destinations) || calls != 4 || !(*first)->discardExpected() ||
            !(*second)->discardExpected() || !graph.restoreUnsubmittedExpected(std::move(*checkpoint)) ||
            (*image)->captureState() != original)
        {
            return std::unexpected("Routed retry/discard did not restore its checkpoint");
        }
        std::puts("Explicit destinations reject invalid plans and restore unsubmitted multi-buffer states");
        return {};
    }

    std::expected<void, std::string> verifyBindings(Device& gpu)
    {
        auto desc                = colorTexture({8, 8});
        desc.mipNum              = 3;
        desc.layerNum            = 2;
        auto first               = Texture::create(gpu, desc);
        auto second              = Texture::create(gpu, desc);
        auto fewerLayersDesc     = desc;
        fewerLayersDesc.layerNum = 1;
        auto fewerLayers         = Texture::create(gpu, fewerLayersDesc);
        auto arrayDesc           = desc;
        arrayDesc.type           = VriTextureType_2DArray;
        auto explicitArray       = Texture::create(gpu, arrayDesc);
        if (!first || !second || !fewerLayers || !explicitArray)
        {
            return std::unexpected("Create rebinding fixture failed");
        }
        RenderGraph graph(gpu);
        auto        resource = graph.importResourceExpected("rebound", **first);
        auto        other    = graph.importResourceExpected("other", **second);
        if (!resource || !other || !graph.exportResourceExpected(*resource) || !graph.compileExpected())
        {
            return std::unexpected("Compile imported rebinding fixture failed");
        }
        if (graph.bindExpected(*resource, **fewerLayers) || graph.bindExpected(*resource, **explicitArray) ||
            graph.bindExpected(*resource, **second) || graph.getTextureExpected(*resource) != first->get())
        {
            return std::unexpected("Rejected rebinding changed identity or accepted incompatible/aliased shapes");
        }
        auto anotherDevice = Device::create(true);
        if (!anotherDevice)
        {
            return std::unexpected(anotherDevice.error());
        }
        auto foreignTexture = Texture::create(**anotherDevice, desc);
        if (!foreignTexture || graph.bindExpected(*resource, **foreignTexture) ||
            graph.getTextureExpected(*resource) != first->get())
        {
            return std::unexpected("Rebinding accepted another device or changed identity on rejection");
        }
        auto third      = Texture::create(gpu, desc);
        auto checkpoint = graph.checkpointExpected();
        if (!third || !checkpoint || !graph.bindExpected(*resource, **third) ||
            graph.getTextureExpected(*resource) != third->get() ||
            graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("Valid rebinding failed or a checkpoint accepted another texture's identity");
        }
        auto volumeDesc     = desc;
        volumeDesc.type     = VriTextureType_3D;
        volumeDesc.layerNum = 1;
        volumeDesc.depth    = 2;
        volumeDesc.usage &= ~VriTextureUsage_ColorAttachment;
        auto volume      = Texture::create(gpu, volumeDesc);
        volumeDesc.depth = 4;
        auto deeper      = Texture::create(gpu, volumeDesc);
        if (!volume || !deeper)
        {
            return std::unexpected("Create volume rebinding fixture failed");
        }
        auto volumeResource = graph.importResourceExpected("volume", **volume);
        if (!volumeResource || graph.bindExpected(*volumeResource, **deeper))
        {
            return std::unexpected("Rebinding changed the compiled volume depth");
        }
        RenderGraph foreign(gpu);
        auto        foreignResource = foreign.importResourceExpected("foreign", **third);
        if (!foreignResource || graph.bindExpected(*foreignResource, **third))
        {
            return std::unexpected("Rebinding accepted a foreign graph resource");
        }
        return {};
    }

    std::expected<void, std::string> verifyMixedGraphState(Device& gpu)
    {
        auto desc    = colorTexture({8, 8});
        desc.mipNum  = 3;
        auto texture = Texture::create(gpu, desc);
        auto frame   = Frame::create(gpu);
        if (!texture || !frame)
        {
            return std::unexpected("Create mixed graph resources failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        (*texture)->transition(*commands,
                               {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        VriTextureViewDesc middle {};
        middle.viewType = VriTextureViewType_MaxEnum;
        middle.baseMip  = 1;
        middle.mipNum   = 1;
        middle.layerNum = 1;
        if (auto changed = (*texture)->transitionExpected(
                *commands,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader},
                middle);
            !changed)
        {
            return changed;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        if ((*texture)->stateExpected())
        {
            return std::unexpected("Seeded mixed mip states were reported as uniform");
        }
        const auto  original = (*texture)->captureState();
        RenderGraph graph(gpu);
        auto        resource = graph.importResourceExpected("mixed mips", **texture);
        if (!resource)
        {
            return std::unexpected(resource.error());
        }
        bool             fail = true;
        const std::array uses {RenderGraph::Use {*resource, Usage::eSampled}};
        if (!graph.addPassExpected(
                "read all mips",
                uses,
                [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    return fail ? std::unexpected("mixed-state failure") : std::expected<void, std::string> {};
                },
                true) ||
            !graph.compileExpected())
        {
            return std::unexpected("Compile mixed mip graph failed");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto failed = graph.executeExpected(*commands);
        if (failed || (*texture)->captureState() != original)
        {
            return std::unexpected("Graph failure did not restore all mip states");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        auto checkpoint = graph.checkpointExpected();
        if (!checkpoint)
        {
            return std::unexpected(checkpoint.error());
        }
        fail     = false;
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto executed = graph.executeExpected(*commands); !executed)
        {
            return executed;
        }
        middle.baseMip = 0;
        if (auto changed = (*texture)->transitionExpected(
                *commands,
                {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                middle);
            !changed)
        {
            return changed;
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
        {
            return restored;
        }
        if ((*texture)->captureState() != original)
        {
            return std::unexpected("Unsubmitted graph checkpoint lost mixed mip states");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto executed = graph.executeExpected(*commands); !executed)
        {
            return executed;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        const auto sampled = (*texture)->stateExpected();
        if (!sampled || sampled->layout != VriLayout_ShaderResource)
        {
            return std::unexpected("Mixed graph retry failed to converge to the recorded full-resource state");
        }
        return {};
    }

    std::expected<void, std::string> verifyVulkanFamilyLifetime(Device& gpu, Frame& graphics, Frame& compute)
    {
        using Use                 = RenderGraph::Use;
        constexpr uint64_t  bytes = 32;
        const VriBufferDesc bufferDesc {bytes,
                                        0,
                                        VriBufferUsage_TransferDst | VriBufferUsage_TransferSrc,
                                        VriMemoryLocation_Device};
        auto                imageDesc = colorTexture({8, 8});
        imageDesc.mipNum              = 2;
        imageDesc.usage |= VriTextureUsage_ShaderResourceStorage;
        auto upload = Buffer::create(
            gpu,
            {bytes, 0, VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostUpload});
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        auto* mapped = gpu.core.MapBuffer((*upload)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map family lifetime upload failed");
        }
        std::memset(mapped, 0x6b, bytes);
        gpu.core.UnmapBuffer((*upload)->handle);
        for (bool image : {false, true})
        {
            RenderGraph graph(gpu);
            auto        resource = image ? graph.createTextureExpected("resource", imageDesc) :
                                           graph.createBufferExpected("resource", bufferDesc);
            auto        source   = graph.importResourceExpected("upload", **upload);
            if (!resource || !source)
            {
                return std::unexpected("Create Vulkan family lifetime resources failed");
            }
            const auto addWrite = [&](const char* name, uint32_t part) -> std::expected<void, std::string>
            {
                const RenderGraph::TextureRange mips {part, 1, 0, 1, VriImageAspect_Color};
                const auto range = part == 0 ? RenderGraph::BufferRange {0, 16} : RenderGraph::BufferRange {20, 8};
                const std::array uses {Use {*resource,
                                            Usage::eCopyDestination,
                                            true,
                                            image ? mips : RenderGraph::TextureRange {},
                                            image ? RenderGraph::BufferRange {} : range},
                                       Use {*source, Usage::eCopySource}};
                return graph.addPassExpected(
                    name,
                    uses,
                    [&, part, range](auto* cmd, auto& active) -> std::expected<void, std::string>
                    {
                        if (image)
                        {
                            auto texture = active.getTextureExpected(*resource);
                            if (!texture)
                            {
                                return std::unexpected(texture.error());
                            }
                            return (*texture)->clearStorageExpected(cmd, {}, {part, 1, 0, 1});
                        }
                        auto target = active.getBufferExpected(*resource);
                        if (!target)
                        {
                            return std::unexpected(target.error());
                        }
                        const VriBufferCopyDesc copy {0, range.offset, range.size};
                        gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*upload)->handle, &copy);
                        return {};
                    },
                    true);
            };
            bool fail = true;
            if (!addWrite("first range", 0) ||
                !graph.addPassExpected(
                    "last callback",
                    {},
                    [&](auto*, auto&) -> std::expected<void, std::string>
                    {
                        if (fail)
                        {
                            return std::unexpected("injected final callback failure");
                        }
                        return {};
                    },
                    true) ||
                !graph.compileExpected())
            {
                return std::unexpected("Compile Vulkan family lifetime graph failed");
            }
            auto a = graphics.beginExpected();
            if (!a)
            {
                return std::unexpected(a.error());
            }
            const auto failed = graph.executeExpected(*a);
            if (failed || failed.error().find("injected final callback failure") == std::string::npos ||
                !graphics.discardExpected())
            {
                return std::unexpected("Vulkan family lifetime failed to discard its rejected graphics recording");
            }
            auto*            validationCommands = graphics.recordingDestination();
            const std::array preflightRecordings {RenderGraph::PassRecording {"first range", validationCommands},
                                                  RenderGraph::PassRecording {"last callback", validationCommands}};
            if (auto preflight = graph.validateOrderedRecordingExpected(validationCommands, preflightRecordings, {});
                !preflight)
            {
                return preflight;
            }
            // The first successful recording can still claim the other family.
            fail        = false;
            auto before = graph.checkpointExpected();
            auto b      = compute.beginExpected();
            if (!before || !b || !graph.executeExpected(*b) || !compute.discardExpected() ||
                !graph.restoreUnsubmittedExpected(std::move(*before)))
            {
                return std::unexpected("Unsubmitted Vulkan ownership did not roll back");
            }
            a = graphics.beginExpected();
            if (!a || !graph.executeExpected(*a) || !graphics.submitAndWaitExpected())
            {
                return std::unexpected("Graphics could not claim Vulkan ownership after rollback");
            }
            // Recompilation appends a byte endpoint or mip use while preserving the physical resource.
            if (!addWrite("second range", 1) || !graph.compileExpected())
            {
                return std::unexpected("Recompile Vulkan family lifetime graph failed");
            }
            b = compute.beginExpected();
            if (!b)
            {
                return std::unexpected(b.error());
            }
            const auto moved = graph.executeExpected(*b);
            if (moved ||
                moved.error().find("first range: exclusive resource range crosses Vulkan queue families") ==
                    std::string::npos ||
                !compute.discardExpected())
            {
                return std::unexpected("Recompile forgot a previous Vulkan queue-family owner");
            }
            a = graphics.beginExpected();
            b = compute.beginExpected();
            if (!a || !b)
            {
                return std::unexpected("Begin disjoint Vulkan lifetime retry failed");
            }
            const std::array recordings {RenderGraph::PassRecording {"first range", *a},
                                         RenderGraph::PassRecording {"last callback", *a},
                                         RenderGraph::PassRecording {"second range", *b}};
            if (auto recorded = graph.executeExpected(*a, recordings); !recorded)
            {
                return recorded;
            }
            if (!graphics.submitAndWaitExpected() || !compute.submitAndWaitExpected())
            {
                gpu.waitIdleForDestruction();
                return std::unexpected("Submit disjoint Vulkan lifetime ranges failed");
            }
        }

        RenderGraph history(gpu);
        auto        value = history.createHistoryBufferExpected("exported history", bufferDesc);
        auto        a     = graphics.beginExpected();
        if (!value || !a || !history.exportResourceExpected(*value) || !history.compileExpected() ||
            !history.executeExpected(*a) || !graphics.submitAndWaitExpected() || !history.resetHistoryExpected())
        {
            return std::unexpected("Initialize exported Vulkan history lifetime fixture failed");
        }
        auto b = compute.beginExpected();
        if (!b)
        {
            return std::unexpected(b.error());
        }
        const auto reset = history.executeExpected(*b);
        const auto state = history.bufferHistoryInfoExpected(*value, {});
        if (reset ||
            reset.error().find("Reset graph history: exclusive resource range crosses Vulkan queue families") ==
                std::string::npos ||
            !state || !state->resetPending || !compute.discardExpected())
        {
            return std::unexpected("Exported history reset escaped Vulkan queue-family preflight");
        }
        a = graphics.beginExpected();
        if (!a || !history.executeExpected(*a) || !graphics.submitAndWaitExpected())
        {
            return std::unexpected("Retry exported Vulkan history reset on its original family failed");
        }
        std::puts("GRAPH_VULKAN_FAMILY_LIFETIME buffer=1 mip=1 history=1 rollback=1 recompile=1");
        return {};
    }

    std::expected<void, std::string> verifyTrackedImports(Device& gpu, Frame& graphics, Frame& compute)
    {
        using Use                 = RenderGraph::Use;
        constexpr auto     policy = RenderGraph::ImportOwnership::eTrackFamilies;
        constexpr uint32_t value  = 0x12345678;
        constexpr uint64_t bytes  = 4 * sizeof(value);
        const uint64_t     row =
            std::max(uint64_t(8), uint64_t(gpu.core.GetDeviceDesc(gpu.handle)->uploadBufferTextureRowAlignment));
        const VriBufferDesc bufferDesc {bytes * 2,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                        VriMemoryLocation_Device};
        auto                imageDesc = colorTexture({4, 4}, VriFormat_R32_UINT);
        imageDesc.mipNum              = 2;
        imageDesc.usage |=
            VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst | VriTextureUsage_ShaderResourceStorage;
        auto upload = Buffer::create(
            gpu,
            {bytes, 0, VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostUpload});
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        const std::array words {value, value, value, value};
        auto*            mapped = gpu.core.MapBuffer((*upload)->handle, 0, bytes);
        if (!mapped)
        {
            return std::unexpected("Map tracked import upload failed");
        }
        std::memcpy(mapped, words.data(), bytes);
        gpu.core.UnmapBuffer((*upload)->handle);

        for (const bool image : {false, true})
        {
            constexpr RenderGraph::TextureRange mip {1, 1, 0, 1, VriImageAspect_Color};
            constexpr RenderGraph::BufferRange  range {bytes, bytes};
            const auto write = [&](RenderGraph& graph, RenderGraph::Resource target) -> std::expected<void, std::string>
            {
                auto source = graph.importResourceExpected("upload", **upload);
                if (!source)
                {
                    return std::unexpected(source.error());
                }
                const std::array uses {Use {target,
                                            Usage::eCopyDestination,
                                            true,
                                            image ? mip : RenderGraph::TextureRange {},
                                            image ? RenderGraph::BufferRange {} : range},
                                       Use {*source, Usage::eCopySource}};
                return graph.addPassExpected(
                    "write tracked range",
                    uses,
                    [&, target](auto* commands, auto& active) -> std::expected<void, std::string>
                    {
                        if (image)
                        {
                            auto texture = active.getTextureExpected(target);
                            if (!texture)
                            {
                                return std::unexpected(texture.error());
                            }
                            VriClearColor clear {};
                            clear.u32[0] = value;
                            return (*texture)->clearStorageExpected(commands, clear, {1, 1, 0, 1});
                        }
                        auto buffer = active.getBufferExpected(target);
                        if (!buffer)
                        {
                            return std::unexpected(buffer.error());
                        }
                        const VriBufferCopyDesc copy {0, bytes, bytes};
                        gpu.core.CmdCopyBuffer(commands, (*buffer)->handle, (*upload)->handle, &copy);
                        return {};
                    });
            };
            RenderGraph producer(gpu);
            auto        output = image ? producer.createTextureExpected("exported texture", imageDesc) :
                                         producer.createBufferExpected("exported buffer", bufferDesc);
            if (!output || !write(producer, *output))
            {
                return std::unexpected("Create tracked import producer failed");
            }
            const RenderGraph::ResourceRange exported {*output,
                                                       image ? mip : RenderGraph::TextureRange {},
                                                       image ? RenderGraph::BufferRange {} : range};
            if (!producer.exportRangeExpected(exported) || !producer.compileExpected())
            {
                return std::unexpected("Compile tracked import producer failed");
            }
            auto first = graphics.beginExpected();
            if (!first || !producer.executeExpected(*first) || !graphics.submitAndWaitExpected())
            {
                return std::unexpected("Submit tracked import producer failed");
            }

            auto originalImage =
                image ? producer.getTextureExpected(*output) : std::expected<Texture*, std::string>(nullptr);
            auto originalBuffer =
                image ? std::expected<Buffer*, std::string>(nullptr) : producer.getBufferExpected(*output);
            auto freshImage =
                image ? Texture::create(gpu, imageDesc) : std::expected<std::unique_ptr<Texture>, std::string>();
            auto freshBuffer =
                image ? std::expected<std::unique_ptr<Buffer>, std::string>() : Buffer::create(gpu, bufferDesc);
            if (!originalImage || !originalBuffer || !freshImage || !freshBuffer)
            {
                return std::unexpected("Create tracked import rebind control failed");
            }

            RenderGraph consumer(gpu);
            auto imported = image ? consumer.importResourceExpected("tracked import", **originalImage, false, policy) :
                                    consumer.importResourceExpected("tracked import", **originalBuffer, false, policy);
            auto readback = consumer.createBufferExpected("tracked readback",
                                                          {row * 2,
                                                           0,
                                                           VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                           VriMemoryLocation_HostReadback});
            if (!imported || !readback || !write(consumer, *imported))
            {
                return std::unexpected("Create tracked import consumer failed");
            }
            const std::array uses {Use {*imported,
                                        Usage::eCopySource,
                                        false,
                                        image ? mip : RenderGraph::TextureRange {},
                                        image ? RenderGraph::BufferRange {} : range},
                                   Use {*readback, Usage::eCopyDestination, true}};
            unsigned         callbacks = 0;
            if (!consumer.addPassExpected(
                    "read tracked range",
                    uses,
                    [&](auto* commands, auto& active) -> std::expected<void, std::string>
                    {
                        ++callbacks;
                        auto target = active.getBufferExpected(*readback);
                        if (!target)
                        {
                            return std::unexpected(target.error());
                        }
                        if (image)
                        {
                            auto source = active.getTextureExpected(*imported);
                            if (!source)
                            {
                                return std::unexpected(source.error());
                            }
                            VriBufferTextureCopyDesc copy {};
                            copy.bufferRowLength   = uint32_t(row / sizeof(value));
                            copy.bufferImageHeight = 2;
                            copy.texture           = {1, 0, 1, VriImageAspect_Color, 0, 0, 0, 2, 2, 1};
                            gpu.core.CmdReadbackTextureToBuffer(commands, (*target)->handle, (*source)->handle, &copy);
                        }
                        else
                        {
                            auto source = active.getBufferExpected(*imported);
                            if (!source)
                            {
                                return std::unexpected(source.error());
                            }
                            const VriBufferCopyDesc copy {bytes, 0, bytes};
                            gpu.core.CmdCopyBuffer(commands, (*target)->handle, (*source)->handle, &copy);
                        }
                        return {};
                    }) ||
                !consumer.exportResourceExpected(*readback) || !consumer.compileExpected())
            {
                return std::unexpected("Compile tracked import consumer failed");
            }
            const auto reject = [&](Frame& frame)
            {
                auto*            cmd = frame.recordingDestination();
                const std::array recordings {RenderGraph::PassRecording {"write tracked range", cmd},
                                             RenderGraph::PassRecording {"read tracked range", cmd}};
                auto             checked = consumer.validateOrderedRecordingExpected(cmd, recordings, {});
                return !checked && checked.error().find("exclusive resource range crosses Vulkan queue families") !=
                                       std::string::npos;
            };
            const auto bind = [&](bool fresh)
            {
                if (image)
                {
                    return consumer.bindExpected(*imported, fresh ? **freshImage : **originalImage);
                }
                return consumer.bindExpected(*imported, fresh ? **freshBuffer : **originalBuffer);
            };
            if (!reject(compute) || callbacks)
            {
                return std::unexpected("Tracked import did not inherit the producing graph's owner");
            }
            first = graphics.beginExpected();
            if (!first || !consumer.executeExpected(*first) || !graphics.submitAndWaitExpected() || !bind(true))
            {
                return std::unexpected("Tracked import could not execute on the producing family's queue");
            }
            auto checkpoint = consumer.checkpointExpected();
            first           = graphics.beginExpected();
            if (!checkpoint || !first || !consumer.executeExpected(*first) || !graphics.discardExpected() ||
                !consumer.restoreUnsubmittedExpected(std::move(*checkpoint)))
            {
                return std::unexpected("Discard tracked import recording failed");
            }
            auto next = compute.beginExpected();
            if (!next || !consumer.executeExpected(*next) || !compute.submitAndWaitExpected() || !reject(graphics) ||
                !bind(false) || !reject(compute) || !bind(true) || !reject(graphics))
            {
                return std::unexpected("Tracked import ownership did not follow rebind or rollback");
            }
            auto result = consumer.getBufferExpected(*readback);
            if (!result)
            {
                return std::unexpected(result.error());
            }
            const auto* data = static_cast<const std::byte*>(gpu.core.MapBuffer((*result)->handle, 0, row * 2));
            if (!data)
            {
                return std::unexpected("Map tracked import readback failed");
            }
            const bool exact = image ? std::memcmp(data, words.data(), 2 * sizeof(value)) == 0 &&
                                           std::memcmp(data + row, words.data(), 2 * sizeof(value)) == 0 :
                                       std::memcmp(data, words.data(), bytes) == 0;
            gpu.core.UnmapBuffer((*result)->handle);
            if (!exact || callbacks != 3)
            {
                return std::unexpected("Tracked import readback or callback count differs");
            }
        }
        std::puts("GRAPH_VULKAN_TRACKED_IMPORTS buffer=1 mip=1 export=1 rollback=1 rebind=1 words=8");
        return {};
    }

    std::expected<void, std::string>
    verifyVulkanTransfers(Device& gpu, Frame& graphics, Frame& compute, bool depthStencil)
    {
        using Use                                                = RenderGraph::Use;
        constexpr auto                                   tracked = RenderGraph::ImportOwnership::eTrackFamilies;
        constexpr std::array<std::array<uint32_t, 4>, 2> words {{{1, 23, 456, 7890}, {81, 19, 14, 72}}};
        constexpr std::array<uint32_t, 3>                colors {0x12345678, 0xfedcba98, 0xabcde123};
        constexpr std::array<float, 3>                   depths {0.375f, 0.625f, 0.75f};
        constexpr std::array<uint8_t, 3>                 stencils {13, 97, 154};
        constexpr auto                                   bytes = sizeof(words.front());
        const VriImageAspectFlags primary = depthStencil ? VriImageAspect_Depth : VriImageAspect_Color;
        const VriImageAspectFlags aspects = primary | (depthStencil ? VriImageAspect_Stencil : VriImageAspect_None);
        const RenderGraph::TextureRange selectedMip {1, 1, 1, 1, aspects};
        const RenderGraph::TextureRange siblingMip {1, 1, 0, 1, aspects};
        constexpr VriAccessStage        readBuffer {VriAccess_CopySourceRead, VriPipelineStage_Transfer};
        constexpr VriAccessLayoutStage  readImage {VriAccess_CopySourceRead,
                                                  VriLayout_CopySource,
                                                  VriPipelineStage_Transfer};
        const auto                      row =
            std::max(uint64_t(8), uint64_t(gpu.core.GetDeviceDesc(gpu.handle)->uploadBufferTextureRowAlignment));
        const auto  offset     = std::max(row, uint64_t(256));
        const auto  resultSize = offset + (depthStencil ? 4 : 2) * row;
        RenderGraph producer(gpu);
        auto upload = Buffer::create(gpu, {sizeof(words), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto buffer = producer.createBufferExpected(
            "exclusive buffer",
            {2 * sizeof(words), 0, VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst, VriMemoryLocation_Device});
        auto desc     = colorTexture({4, 4}, depthStencil ? VriFormat_D32_SFLOAT_S8_UINT : VriFormat_R32_UINT);
        desc.type     = VriTextureType_2DArray;
        desc.mipNum   = 2;
        desc.layerNum = uint32_t(colors.size());
        desc.usage    = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst |
                     (depthStencil ? VriTextureUsage_DepthStencilAttachment : VriTextureUsage_ShaderResourceStorage);
        auto texture = producer.createTextureExpected("exclusive array", desc);
        if (!upload || !buffer || !texture)
        {
            return std::unexpected("Create exclusive queue-transfer resources failed");
        }
        auto* mapped = gpu.core.MapBuffer((*upload)->handle, 0, sizeof(words));
        if (!mapped)
        {
            return std::unexpected("Map queue-transfer upload failed");
        }
        std::memcpy(mapped, words.data(), sizeof(words));
        gpu.core.UnmapBuffer((*upload)->handle);
        auto input = producer.importResourceExpected("upload", **upload);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        const std::array writes {Use {*input, Usage::eCopySource},
                                 Use {*buffer, Usage::eCopyDestination, true},
                                 Use {*texture, Usage::eCopyDestination, true, {1, 1, 0, desc.layerNum, aspects}}};
        if (!producer.addPassExpected(
                "write bytes and layers",
                writes,
                [&](auto* command, auto& graph) -> std::expected<void, std::string>
                {
                    auto buf = graph.getBufferExpected(*buffer);
                    auto tex = graph.getTextureExpected(*texture);
                    if (!buf || !tex)
                    {
                        return std::unexpected(buf ? tex.error() : buf.error());
                    }
                    const VriBufferCopyDesc copy {0, 0, sizeof(words)};
                    gpu.core.CmdCopyBuffer(command, (*buf)->handle, (*upload)->handle, &copy);
                    const VriBufferCopyDesc extra {0, sizeof(words), sizeof(words)};
                    gpu.core.CmdCopyBuffer(command, (*buf)->handle, (*upload)->handle, &extra);
                    for (uint32_t layer = 0; layer < colors.size(); ++layer)
                    {
                        const VriTextureSubresourceRange range {1, 1, layer, 1};
                        if (depthStencil)
                        {
                            if (auto clear =
                                    (*tex)->clearDepthStencilExpected(command, {depths[layer], stencils[layer]}, range);
                                !clear)
                            {
                                return clear;
                            }
                        }
                        else
                        {
                            VriClearColor value {};
                            value.u32[0] = colors[layer];
                            if (auto clear = (*tex)->clearStorageExpected(command, value, range); !clear)
                            {
                                return clear;
                            }
                        }
                    }
                    return {};
                }) ||
            !producer.exportRangeExpected({*buffer, {}, {3 * bytes, bytes}}) ||
            !producer.exportRangeExpected({*buffer, {}, {bytes + bytes / 2, bytes / 2}}) ||
            !producer.exportRangeExpected({*buffer, {}, {0, bytes + bytes / 2}}) ||
            !producer.exportRangeExpected({*texture, siblingMip}) ||
            !producer.exportRangeExpected({*texture, {1, 1, 1, 1, primary}}) ||
            (depthStencil && (!producer.exportRangeExpected({*texture, {1, 1, 1, 1, VriImageAspect_Stencil}}) ||
                              !producer.exportRangeExpected({*texture, {1, 1, 2, 1, VriImageAspect_Depth}}))))
        {
            return std::unexpected("Compile exclusive transfer producer failed");
        }
        if (auto compiled = depthStencil ? producer.compileHeapsExpected() : producer.compileExpected(); !compiled)
        {
            return compiled;
        }
        auto release = graphics.beginExpected();
        if (!release || !producer.executeExpected(*release) || !graphics.submitAndWaitExpected())
        {
            return std::unexpected("Submit exclusive transfer producer failed");
        }
        auto sourceBuffer = producer.getBufferExpected(*buffer);
        auto sourceImage  = producer.getTextureExpected(*texture);
        if (!sourceBuffer || !sourceImage)
        {
            return std::unexpected(sourceBuffer ? sourceImage.error() : sourceBuffer.error());
        }

        RenderGraph reader(gpu);
        RenderGraph sibling(gpu);
        unsigned    copies       = 0;
        const auto  readerOutput = [&](RenderGraph&              graph,
                                      RenderGraph::BufferRange  byteRange,
                                      RenderGraph::TextureRange mip) -> std::expected<Buffer*, std::string>
        {
            auto buf    = graph.importResourceExpected("read buffer", **sourceBuffer, true, tracked);
            auto tex    = graph.importResourceExpected("read texture", **sourceImage, true, tracked);
            auto output = graph.createBufferExpected("readback",
                                                     {resultSize,
                                                      0,
                                                      VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                      VriMemoryLocation_HostReadback});
            if (!buf || !tex || !output)
            {
                return std::unexpected("Create queue-transfer receiver failed");
            }
            const std::array uses {Use {*buf, Usage::eCopySource, false, {}, byteRange},
                                   Use {*tex, Usage::eCopySource, false, mip},
                                   Use {*output, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected(
                    "read transferred data",
                    uses,
                    [&, output = *output, byteRange, mip](auto* command,
                                                          auto& active) -> std::expected<void, std::string>
                    {
                        auto target = active.getBufferExpected(output);
                        if (!target)
                        {
                            return std::unexpected(target.error());
                        }
                        ++copies;
                        const VriBufferCopyDesc copy {byteRange.offset, 0, byteRange.size};
                        gpu.core.CmdCopyBuffer(command, (*target)->handle, (*sourceBuffer)->handle, &copy);
                        VriBufferTextureCopyDesc region {offset,
                                                         uint32_t(row / sizeof(uint32_t)),
                                                         2,
                                                         {1, mip.baseLayer, 1, primary, 0, 0, 0, 2, 2, 1}};
                        gpu.core.CmdReadbackTextureToBuffer(command,
                                                            (*target)->handle,
                                                            (*sourceImage)->handle,
                                                            &region);
                        if (depthStencil)
                        {
                            region.bufferOffset += 2 * row;
                            region.bufferRowLength = uint32_t(row);
                            region.texture.aspect  = VriImageAspect_Stencil;
                            gpu.core.CmdReadbackTextureToBuffer(command,
                                                                (*target)->handle,
                                                                (*sourceImage)->handle,
                                                                &region);
                        }
                        return {};
                    }) ||
                !graph.exportResourceExpected(*output) || !graph.compileExpected())
            {
                return std::unexpected("Compile queue-transfer receiver failed");
            }
            return graph.getBufferExpected(*output);
        };
        auto selectedOutput = readerOutput(reader, {bytes, bytes}, selectedMip);
        auto siblingOutput  = readerOutput(sibling, {0, bytes}, siblingMip);
        if (!selectedOutput || !siblingOutput)
        {
            return std::unexpected(selectedOutput ? siblingOutput.error() : selectedOutput.error());
        }
        const auto accepts = [](RenderGraph& graph, VriCommandBuffer* command)
        {
            const std::array recordings {RenderGraph::PassRecording {"read transferred data", command}};
            return graph.validateOrderedRecordingExpected(command, recordings, {}).has_value();
        };
        const auto handoff = [&](VriCommandBuffer* src, VriCommandBuffer* dst) -> std::expected<void, std::string>
        {
            if (auto transferred =
                    producer.transferBufferOwnershipExpected(*buffer, {bytes, bytes}, src, dst, readBuffer);
                !transferred)
            {
                return transferred;
            }
            return producer.transferTextureOwnershipExpected(*texture, selectedMip, src, dst, readImage);
        };
        const std::array<const Frame*, 2> frames {&graphics, &compute};
        release         = graphics.beginExpected();
        auto acquire    = compute.beginExpected();
        auto checkpoint = producer.checkpointExpected(frames);
        if (!checkpoint || !release || !acquire || accepts(reader, *acquire) || !accepts(sibling, *release))
        {
            return std::unexpected("Queue-transfer fixture did not start with exclusive graphics ownership");
        }

        const VriBufferBarrierDesc  valid {(*sourceBuffer)->handle,
                                           {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer},
                                          readBuffer,
                                          bytes,
                                          bytes};
        const VriTextureBarrierDesc invalid {
            (*sourceImage)->handle,
            {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
            readImage,
            aspects,
            2,
            1,
            1,
            1};
        const VriBarrierGroupDesc validBuffer {&valid, 1, nullptr, 0};
        const VriBarrierGroupDesc rejected {&valid, 1, &invalid, 1};
        if (gpu.core.CmdQueueFamilyTransfer(*release, *acquire, &rejected) != VriResult_InvalidArgument ||
            producer.transferBufferOwnershipExpected(*buffer, {bytes, bytes}, *release, *release, readBuffer) ||
            producer.transferTextureOwnershipExpected(*texture, {0, 1, 1, 1, aspects}, *release, *acquire, readImage))
        {
            return std::unexpected("Invalid queue transfer was accepted");
        }
        const auto bufferState = (*sourceBuffer)->captureState();
        const auto imageState  = (*sourceImage)->captureState();
        // The entire request is initialized and graphics-owned. Only the declared exports exclude the hole.
        auto bufferHole = producer.transferBufferOwnershipExpected(*buffer,
                                                                   {2 * bytes - 4, bytes + 8},
                                                                   *release,
                                                                   *acquire,
                                                                   readBuffer);
        auto imageHole =
            producer.transferTextureOwnershipExpected(*texture, {1, 1, 1, 2, aspects}, *release, *acquire, readImage);
        if (bufferHole || imageHole)
        {
            return std::unexpected("Transferred graph-owned data outside its exported ranges");
        }
        if (!bufferHole.error().contains("not fully exported") || !imageHole.error().contains("not fully exported"))
        {
            return std::unexpected("Queue transfer export holes did not report range coverage");
        }
        if ((*sourceBuffer)->captureState() != bufferState || (*sourceImage)->captureState() != imageState ||
            !accepts(sibling, *release) || accepts(reader, *acquire))
        {
            return std::unexpected("Rejected export range changed queue ownership or resource state");
        }
        if (auto transferred = handoff(*release, *acquire); !transferred)
        {
            return transferred;
        }
        if (!accepts(reader, *acquire) || accepts(reader, *release) || accepts(sibling, *acquire) ||
            !accepts(sibling, *release) || copies || producer.restoreUnsubmittedExpected(std::move(*checkpoint)) ||
            !compute.discardExpected())
        {
            return std::unexpected("Queue transfer moved an adjacent range or did not discard cleanly");
        }
        const auto reset     = std::exchange(gpu.core.ResetCommandAllocatorChecked, rejectAllocatorReset);
        const auto discarded = graphics.discardExpected();
        gpu.core.ResetCommandAllocatorChecked = reset;
        if (discarded || discarded.error().find("Reset command allocator failed") == std::string::npos ||
            producer.restoreUnsubmittedExpected(std::move(*checkpoint)) || graphics.submitExpected() ||
            !graphics.discardExpected() || !producer.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("A failed allocator reset permitted restore or submission of a discarded pair");
        }

        release = graphics.beginExpected();
        if (!release || gpu.core.CmdQueueFamilyTransfer(*release, compute.recordingDestination(), &validBuffer) !=
                            VriResult_InvalidArgument)
        {
            return std::unexpected("Queue transfer accepted an idle acquire command buffer");
        }
        acquire = compute.beginExpected();
        if (!acquire || accepts(reader, *acquire) || !accepts(reader, *release) ||
            gpu.core.CmdQueueFamilyTransfer(*release, *acquire, &rejected) != VriResult_InvalidArgument)
        {
            return std::unexpected("Queue transfer rollback did not restore ownership");
        }
        auto accepted = producer.checkpointExpected(frames);
        if (!accepted)
        {
            return std::unexpected(accepted.error());
        }
        if (auto transferred = handoff(*release, *acquire); !transferred)
        {
            return transferred;
        }
        if (!sibling.executeExpected(*release) || !reader.executeExpected(*acquire))
        {
            return std::unexpected("Record queue-transfer range reads failed");
        }
        VriFence*  rawFence = nullptr;
        const auto created  = gpu.core.CreateFence(gpu.handle, 0, &rawFence);
        const std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(rawFence,
                                                                                        gpu.core.DestroyFence);
        if (created != VriResult_Success || !fence)
        {
            return std::unexpected("Create queue-transfer fence failed");
        }
        const std::array dependency {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands}};
        const auto       rejectAcquire = [&](std::span<const VriFenceSubmitDesc> waits)
        {
            const auto native    = std::exchange(gpu.core.QueueSubmit, rejectQueueSubmission);
            auto       result    = compute.submitExpected(waits);
            gpu.core.QueueSubmit = native;
            return result;
        };
        if (!graphics.submitExpected({}, dependency))
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit queue-transfer release failed");
        }
        const auto rejectedAcquire = rejectAcquire(dependency);
        if (rejectedAcquire || rejectedAcquire.error().find("Submit frame failed") == std::string::npos ||
            producer.restoreUnsubmittedExpected(std::move(*accepted)) || !graphics.waitExpected() ||
            !compute.submitExpected(dependency) || !compute.waitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Retry queue-transfer acquire after an accepted release failed");
        }
        const auto verifyReadback = [&](uint32_t i) -> std::expected<void, std::string>
        {
            const auto  handle = (i ? *selectedOutput : *siblingOutput)->handle;
            const auto* data   = static_cast<const std::byte*>(gpu.core.MapBuffer(handle, 0, resultSize));
            if (!data)
            {
                return std::unexpected("Map transferred bytes and texture failed");
            }
            const auto       texel = depthStencil ? std::bit_cast<uint32_t>(depths[i]) : colors[i];
            const std::array texels {texel, texel};
            const std::array stencilTexels {stencils[i], stencils[i]};
            const bool       exact =
                std::memcmp(data, words[i].data(), bytes) == 0 &&
                std::memcmp(data + offset, texels.data(), sizeof(texels)) == 0 &&
                std::memcmp(data + offset + row, texels.data(), sizeof(texels)) == 0 &&
                (!depthStencil ||
                 (std::memcmp(data + offset + 2 * row, stencilTexels.data(), sizeof(stencilTexels)) == 0 &&
                  std::memcmp(data + offset + 3 * row, stencilTexels.data(), sizeof(stencilTexels)) == 0));
            gpu.core.UnmapBuffer(handle);
            if (!exact)
            {
                return std::unexpected("Queue-family transfer did not preserve the selected or adjacent data");
            }
            return {};
        };
        for (uint32_t i = 0; i < words.size(); ++i)
        {
            if (auto verified = verifyReadback(i); !verified)
            {
                return verified;
            }
        }

        // Transfer the selected ranges back through the consumer's tracked imports.
        auto bufferImport = reader.findResourceExpected("read buffer");
        auto imageImport  = reader.findResourceExpected("read texture");
        release           = compute.beginExpected();
        acquire           = graphics.beginExpected();
        if (!bufferImport || !imageImport || !release || !acquire ||
            !reader.transferBufferOwnershipExpected(*bufferImport, {bytes, bytes}, *release, *acquire, readBuffer) ||
            !reader.transferTextureOwnershipExpected(*imageImport, selectedMip, *release, *acquire, readImage) ||
            !reader.executeExpected(*acquire) || copies != 3)
        {
            return std::unexpected("Queue transfer through tracked imports failed");
        }
        const std::array returnOrder {VriFenceSubmitDesc {fence.get(), 2, VriPipelineStage_AllCommands}};
        if (!compute.submitExpected({}, returnOrder) || !graphics.submitExpected(returnOrder) ||
            !graphics.waitExpected() || !compute.waitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit reverse queue transfer failed");
        }
        if (auto verified = verifyReadback(1); !verified)
        {
            return verified;
        }
        // Abandon a second acquire only after retiring the accepted release. Neither idle Frame is proof of rollback.
        if (!depthStencil)
        {
            release                   = graphics.beginExpected();
            acquire                   = compute.beginExpected();
            auto             terminal = producer.checkpointExpected(frames);
            const std::array order {VriFenceSubmitDesc {fence.get(), 3, VriPipelineStage_AllCommands}};
            if (!release || !acquire || !terminal || !handoff(*release, *acquire) ||
                !graphics.submitExpected({}, order) || rejectAcquire(order) || !compute.discardExpected() ||
                !graphics.waitExpected() || !graphics.discardExpected() ||
                producer.restoreUnsubmittedExpected(std::move(*terminal)))
            {
                gpu.waitIdleForDestruction();
                return std::unexpected("An accepted release allowed rollback after an abandoned acquire");
            }
            std::puts("GRAPH_PARTIAL_HANDOFF acquire_discarded=1 accepted_release_rollback_rejected=1");
        }
        std::puts("GRAPH_HANDOFF_RECOVERY reset_retry=1 acquire_retry=1");
        std::printf("GRAPH_TRANSFER_EXPORTS byte_union=1 byte_hole=1 texture_hole=1 aspect_union=%d\n", depthStencil);
        std::printf("GRAPH_VULKAN_TRANSFERS bytes=32 texels=8 mip=1 layers=2 depth_stencil=%d rollback=1 roundtrip=1 "
                    "rejected=4\n",
                    depthStencil);
        return {};
    }

    std::expected<void, std::string> verifyRoutedHistoryClears(Device& gpu)
    {
        using Use                                      = RenderGraph::Use;
        using Placement                                = RenderGraph::HistoryResetRecording;
        constexpr uint32_t                initialColor = 0x13243546;
        constexpr std::array<uint32_t, 2> initialWords {13, 26};
        constexpr std::array<uint32_t, 2> writtenWords {117, 211};
        constexpr std::array              names {"read graphics history",
                                    "write graphics history",
                                    "read compute history",
                                    "write compute history"};
        const auto*                       caps = gpu.core.GetDeviceDesc(gpu.handle);
        const uint64_t                    row  = std::max(uint64_t(8), uint64_t(caps->uploadBufferTextureRowAlignment));
        const uint64_t                    imageOffset = std::max(row, uint64_t(256));
        const uint64_t                    bytes       = imageOffset + 2 * row;
        auto                              upload      = Buffer::create(gpu,
                                                                       {sizeof(writtenWords),
                                                                        0,
                                                                        VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                                                        VriMemoryLocation_HostUpload});
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        auto* mappedUpload = gpu.core.MapBuffer((*upload)->handle, 0, sizeof(writtenWords));
        if (!mappedUpload)
        {
            return std::unexpected("Map authored history failed");
        }
        std::memcpy(mappedUpload, writtenWords.data(), sizeof(writtenWords));
        gpu.core.UnmapBuffer((*upload)->handle);
        (*upload)->setState({VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        RenderGraph graph(gpu);
        auto        buffer = graph.createHistoryBufferExpected("routed history bytes",
                                                               {sizeof(initialWords),
                                                                0,
                                                                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                                                VriMemoryLocation_Device},
                                                        std::as_bytes(std::span(initialWords)));
        auto        desc   = colorTexture({2, 2}, VriFormat_R32_UINT);
        desc.mipNum        = 2;
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        VriClearValue clear {};
        clear.color.u32[0] = initialColor;
        auto texture       = graph.createHistoryTextureExpected("routed history mips", desc, clear);
        auto input         = graph.importResourceExpected("authored history", **upload);
        if (!input || !buffer || !texture)
        {
            return std::unexpected("Create routed history test resources failed");
        }
        auto depthDesc = depthTexture({1, 1});
        depthDesc.usage |= VriTextureUsage_TransferDst;
        auto depth = graph.createHistoryTextureExpected("graphics-only history", depthDesc);
        if (!depth)
        {
            return std::unexpected(depth.error());
        }
        std::array<RenderGraph::Resource, 2>         outputs {};
        std::array<RenderGraph::HistorySelection, 2> computeSelections {};
        bool                                         failCompute = false;
        for (uint32_t i = 0; i < outputs.size(); ++i)
        {
            auto output =
                graph.createBufferExpected("routed readback " + std::to_string(i),
                                           {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!output)
            {
                return std::unexpected(output.error());
            }
            outputs[i] = *output;
            const RenderGraph::BufferRange  words {i * sizeof(uint32_t), sizeof(uint32_t)};
            const RenderGraph::TextureRange mip {i, 1, 0, 1, VriImageAspect_Color};
            if (i == 1)
            {
                computeSelections = {{{*buffer, {}, words}, {*texture, mip}}};
            }
            const std::array reads {Use {*buffer, Usage::eCopySource, false, {}, words},
                                    Use {*texture, Usage::eCopySource, false, mip},
                                    Use {*output, Usage::eCopyDestination, true}};
            const std::array writes {Use {*input, Usage::eCopySource},
                                     Use {*buffer, Usage::eCopyDestination, true, {}, words},
                                     Use {*texture, Usage::eCopyDestination, true, mip}};
            if (!graph.addPassExpected(
                    names[2 * i],
                    reads,
                    [&, i, words, output = *output](auto* cmd, auto& current) -> std::expected<void, std::string>
                    {
                        if (i == 1 && failCompute)
                        {
                            return std::unexpected("reject routed history consumer");
                        }
                        auto source = current.getBufferExpected(*buffer);
                        auto image  = current.getTextureExpected(*texture);
                        auto target = current.getBufferExpected(output);
                        if (!source || !image || !target)
                        {
                            return std::unexpected("Resolve routed history readback failed");
                        }
                        const VriBufferCopyDesc copy {words.offset, 0, words.size};
                        gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*source)->handle, &copy);
                        const auto                     extent = 2u >> i;
                        const VriBufferTextureCopyDesc region {
                            imageOffset,
                            uint32_t(row / 4),
                            extent,
                            {i, 0, 1, VriImageAspect_Color, 0, 0, 0, extent, extent, 1}};
                        gpu.core.CmdReadbackTextureToBuffer(cmd, (*target)->handle, (*image)->handle, &region);
                        return {};
                    }) ||
                !graph.addPassExpected(names[2 * i + 1],
                                       writes,
                                       [&, i, words](auto* cmd, auto& current) -> std::expected<void, std::string>
                                       {
                                           auto target = current.getBufferExpected(*buffer);
                                           auto image  = current.getTextureExpected(*texture);
                                           if (!target || !image)
                                           {
                                               return std::unexpected("Resolve authored history failed");
                                           }
                                           const VriBufferCopyDesc copy {words.offset, words.offset, words.size};
                                           gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*upload)->handle, &copy);
                                           VriClearColor value {};
                                           value.u32[0] = writtenWords[i];
                                           return (*image)->clearStorageExpected(cmd, value, {i, 1, 0, 1});
                                       }) ||
                !graph.exportResourceExpected(*output))
            {
                return std::unexpected("Declare routed history passes failed");
            }
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }

        auto graphics   = Frame::create(gpu);
        auto compute    = Frame::create(gpu, VriQueueType_Compute);
        auto clearFrame = Frame::create(gpu, VriQueueType_Compute);
        if (!graphics || !compute || !clearFrame)
        {
            return std::unexpected("Create routed history Frames failed");
        }
        auto*            a = (*graphics)->recordingDestination();
        auto*            b = (*compute)->recordingDestination();
        auto*            c = (*clearFrame)->recordingDestination();
        const std::array passRecordings {RenderGraph::PassRecording {names[0], a},
                                         RenderGraph::PassRecording {names[1], a},
                                         RenderGraph::PassRecording {names[2], b},
                                         RenderGraph::PassRecording {names[3], b}};
        const std::array placements {Placement {computeSelections[0], clearFrame->get()},
                                     Placement {computeSelections[1], clearFrame->get()}};
        const std::array order {RenderGraph::RecordingDependency {c, b}};
        const std::array duplicate {placements[0], placements[0]};
        const std::array unsupported {Placement {{*depth}, clearFrame->get()}};
        const auto       missing    = graph.validateOrderedRecordingExpected(a, passRecordings, {}, placements);
        const auto       overlap    = graph.validateOrderedRecordingExpected(a, passRecordings, order, duplicate);
        const auto unsupportedDepth = graph.validateOrderedRecordingExpected(a, passRecordings, order, unsupported);
        if (missing || !missing.error().contains("misses a required submission dependency") || overlap ||
            !overlap.error().contains("overlapping history clear placements") || unsupportedDepth ||
            !unsupportedDepth.error().contains("requires a graphics Frame") ||
            !graph.validateOrderedRecordingExpected(a, passRecordings, order, placements) ||
            !graph.resourceInfo(*buffer).historyResetPending || !graph.resourceInfo(*texture).historyResetPending)
        {
            return std::unexpected("History clear placement preflight did not preserve the pending reset contract");
        }
        if (gpu.allowsSeparateComputeFamily())
        {
            const std::array defaultOrder {RenderGraph::RecordingDependency {a, b}};
            const auto       original = graph.validateOrderedRecordingExpected(a, passRecordings, defaultOrder);
            if (original || !original.error().contains("exclusive resource range crosses Vulkan queue families"))
            {
                return std::unexpected("Exclusive compute history unexpectedly accepted a default graphics clear");
            }
        }
        VriFence*  raw     = nullptr;
        const auto created = gpu.core.CreateFence(gpu.handle, 0, &raw);
        const std::unique_ptr<VriFence, decltype(gpu.core.DestroyFence)> fence(raw, gpu.core.DestroyFence);
        if (created != VriResult_Success || !fence)
        {
            return std::unexpected("Create routed history clear completion fence failed");
        }
        const auto begin = [&]
        {
            return (*graphics)->beginExpected() && (*compute)->beginExpected() && (*clearFrame)->beginExpected();
        };
        const auto record = [&]
        {
            return graph.executeOrderedExpected(a, passRecordings, order, nullptr, placements);
        };
        failCompute = true;
        if (!begin())
        {
            return std::unexpected("Begin routed history failure recording failed");
        }
        auto failed = record();
        if (failed || !failed.error().contains("reject routed history consumer") ||
            !graph.resourceInfo(*buffer).historyResetPending || !graph.resourceInfo(*texture).historyResetPending ||
            !(*graphics)->discardExpected() || !(*compute)->discardExpected() || !(*clearFrame)->discardExpected())
        {
            return std::unexpected("Routed history failure lost pending clears or could not discard all destinations");
        }
        failCompute = false;
        if (!begin())
        {
            return std::unexpected("Begin routed history checkpoint failed");
        }
        {
            const std::array<const Frame*, 3> frames {graphics->get(), compute->get(), clearFrame->get()};
            auto                              checkpoint = graph.checkpointExpected(frames);
            auto                              incomplete = graph.checkpointExpected(std::span(frames).first(2));
            if (!checkpoint || !incomplete || !record() || !(*graphics)->discardExpected() ||
                !(*compute)->discardExpected())
            {
                return std::unexpected("Guarded history rollback requires discarding the clear-only Frame");
            }
            const auto omitted = graph.restoreUnsubmittedExpected(std::move(*incomplete));
            if (omitted || !omitted.error().contains("omits a recorded command buffer") ||
                graph.restoreUnsubmittedExpected(std::move(*checkpoint)) || !(*clearFrame)->discardExpected())
            {
                return std::unexpected("A clear-only destination was not covered by the guarded checkpoint");
            }
            if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint));
                !restored || !graph.resourceInfo(*buffer).historyResetPending ||
                !graph.resourceInfo(*texture).historyResetPending)
            {
                return std::unexpected("Guarded history rollback lost the pending clear ranges");
            }
        }
        for (uint64_t cycle = 0; cycle < 2; ++cycle)
        {
            if (cycle)
            {
                if (auto reset = graph.resetHistoryExpected(computeSelections); !reset)
                {
                    return reset;
                }
            }
            if (!begin())
            {
                return std::unexpected("Begin routed history replay failed");
            }
            if (auto recorded = record(); !recorded)
            {
                return recorded;
            }
            const std::array wait {VriFenceSubmitDesc {fence.get(), cycle + 1, VriPipelineStage_AllCommands}};
            if (!(*clearFrame)->submitExpected({}, wait) || !(*compute)->submitExpected(wait) ||
                !(*graphics)->submitAndWaitExpected() || !(*compute)->waitExpected() || !(*clearFrame)->waitExpected())
            {
                gpu.waitIdleForDestruction();
                return std::unexpected("Submit routed history clear and readers failed");
            }
            for (uint32_t i = 0; i < outputs.size(); ++i)
            {
                auto target = graph.getBufferExpected(outputs[i]);
                if (!target)
                {
                    return std::unexpected(target.error());
                }
                const auto* mapped = static_cast<const std::byte*>(gpu.core.MapBuffer((*target)->handle, 0, bytes));
                if (!mapped)
                {
                    return std::unexpected("Map routed history readback failed");
                }
                const bool     retained = cycle != 0 && i == 0;
                const uint32_t word     = retained ? writtenWords[i] : initialWords[i];
                const uint32_t texel    = retained ? writtenWords[i] : initialColor;
                bool           exact    = std::memcmp(mapped, &word, sizeof(word)) == 0;
                const auto     extent   = 2u >> i;
                for (uint32_t y = 0; y < extent; ++y)
                {
                    for (uint32_t x = 0; x < extent; ++x)
                    {
                        exact &=
                            std::memcmp(mapped + imageOffset + y * row + x * sizeof(texel), &texel, sizeof(texel)) == 0;
                    }
                }
                gpu.core.UnmapBuffer((*target)->handle);
                if (!exact)
                {
                    return std::unexpected("Routed history clear changed the wrong buffer word or mip");
                }
            }
        }
        std::puts("GRAPH_ROUTED_HISTORY native_heaps=1 reset_only=1 missing_edge=1 rollback=1 "
                  "partial_reset=1 words=4 texels=10 checkpoint=1");
        return {};
    }

    std::expected<void, std::string> verifyInGraphHandoffs(Device& gpu)
    {
        using Use     = RenderGraph::Use;
        using Handoff = RenderGraph::QueueFamilyHandoff;
        constexpr std::array<uint32_t, 4> words {117, 811, 612, 1239};
        constexpr uint64_t                wordBytes = sizeof(uint32_t);
        constexpr uint64_t                half      = sizeof(words) / 2;
        constexpr std::array<uint32_t, 2> colors {0x1234abcd, 0x98127356};
        constexpr std::array              names {"upload exclusive",
                                    "compute first",
                                    "compute last",
                                    "graphics tail",
                                    "activate successor",
                                    "read successor"};
        const auto*                       caps = gpu.core.GetDeviceDesc(gpu.handle);
        const auto                        row  = std::max(wordBytes, uint64_t(caps->uploadBufferTextureRowAlignment));
        const auto                        imageOffset = std::max(row, uint64_t(256));
        const auto                        readSize    = imageOffset + row + wordBytes;

        auto upload = Buffer::create(gpu, {sizeof(words), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!upload)
        {
            return std::unexpected(upload.error());
        }
        auto* mapped = gpu.core.MapBuffer((*upload)->handle, 0, sizeof(words));
        if (!mapped)
        {
            return std::unexpected("Map in-graph handoff upload failed");
        }
        std::memcpy(mapped, words.data(), sizeof(words));
        gpu.core.UnmapBuffer((*upload)->handle);
        (*upload)->setState({VriAccess_CopySourceRead, VriPipelineStage_Transfer});

        RenderGraph         graph(gpu);
        const VriBufferDesc desc {sizeof(words),
                                  0,
                                  VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                  VriMemoryLocation_Device};
        auto                input     = graph.importResourceExpected("handoff upload", **upload);
        auto                payload   = graph.createBufferExpected("exclusive payload", desc);
        auto                late      = graph.createBufferExpected("heap successor", desc);
        auto                imageDesc = colorTexture({1, 1}, VriFormat_R32_UINT);
        imageDesc.type                = VriTextureType_2DArray;
        imageDesc.layerNum            = 2;
        imageDesc.usage |= VriTextureUsage_ShaderResourceStorage;
        auto image = graph.createTextureExpected("exclusive color layers", imageDesc);
        if (!input || !payload || !late || !image)
        {
            return std::unexpected("Create in-graph handoff resources failed");
        }
        constexpr RenderGraph::BufferRange  selectedBytes {half, half};
        constexpr RenderGraph::TextureRange selectedLayer {0, 1, 1, 1, VriImageAspect_Color};
        const std::array                    firstUses {Use {*input, Usage::eCopySource},
                                    Use {*payload, Usage::eCopyDestination, true},
                                    Use {*image, Usage::eCopyDestination, true}};
        if (auto added = graph.addPassExpected(
                names[0],
                firstUses,
                [&](auto* cmd, auto& current) -> std::expected<void, std::string>
                {
                    auto buffer  = current.getBufferExpected(*payload);
                    auto texture = current.getTextureExpected(*image);
                    if (!buffer || !texture)
                    {
                        return std::unexpected("Resolve upload handoff resources failed");
                    }
                    const VriBufferCopyDesc copy {0, 0, sizeof(words)};
                    gpu.core.CmdCopyBuffer(cmd, (*buffer)->handle, (*upload)->handle, &copy);
                    for (uint32_t layer = 0; layer < colors.size(); ++layer)
                    {
                        VriClearColor color {};
                        color.u32[0] = colors[layer];
                        if (auto cleared = (*texture)->clearStorageExpected(cmd, color, {0, 1, layer, 1}); !cleared)
                        {
                            return cleared;
                        }
                    }
                    return {};
                });
            !added)
        {
            return added;
        }

        std::array<RenderGraph::Resource, 4> outputs {};
        for (uint32_t i = 0; i < outputs.size(); ++i)
        {
            auto output =
                graph.createBufferExpected("handoff readback " + std::to_string(i),
                                           {readSize, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!output)
            {
                return std::unexpected(output.error());
            }
            if (auto exported = graph.exportResourceExpected(*output); !exported)
            {
                return exported;
            }
            outputs[i] = *output;
        }
        bool rejectConsumer = false;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const bool       full = i == 2;
            const std::array uses {
                Use {*payload, Usage::eCopySource, false, {}, full ? RenderGraph::BufferRange {} : selectedBytes},
                Use {*image, Usage::eCopySource, false, full ? RenderGraph::TextureRange {} : selectedLayer},
                Use {outputs[i], Usage::eCopyDestination, true}};
            if (auto added = graph.addPassExpected(
                    names[i + 1],
                    uses,
                    [&, i, full](auto* cmd, auto& current) -> std::expected<void, std::string>
                    {
                        if (i == 0 && rejectConsumer)
                        {
                            return std::unexpected("injected handoff consumer failure");
                        }
                        auto buffer  = current.getBufferExpected(*payload);
                        auto texture = current.getTextureExpected(*image);
                        auto output  = current.getBufferExpected(outputs[i]);
                        if (!buffer || !texture || !output)
                        {
                            return std::unexpected("Resolve handoff readback resources failed");
                        }
                        const VriBufferCopyDesc copy {full ? 0 : half, 0, full ? sizeof(words) : half};
                        gpu.core.CmdCopyBuffer(cmd, (*output)->handle, (*buffer)->handle, &copy);
                        for (uint32_t layer = full ? 0u : 1u; layer < 2; ++layer)
                        {
                            const VriBufferTextureCopyDesc region {
                                imageOffset + (full ? layer * row : 0),
                                uint32_t(row / wordBytes),
                                1,
                                {0, layer, 1, VriImageAspect_Color, 0, 0, 0, 1, 1, 1}};
                            gpu.core.CmdReadbackTextureToBuffer(cmd, (*output)->handle, (*texture)->handle, &region);
                        }
                        return {};
                    });
                !added)
            {
                return added;
            }
        }
        const std::array writeLate {Use {*input, Usage::eCopySource}, Use {*late, Usage::eCopyDestination, true}};
        const std::array readLate {Use {*late, Usage::eCopySource}, Use {outputs[3], Usage::eCopyDestination, true}};
        if (!graph.addPassExpected(names[4],
                                   writeLate,
                                   [&](auto* cmd, auto& current) -> std::expected<void, std::string>
                                   {
                                       auto target = current.getBufferExpected(*late);
                                       if (!target)
                                       {
                                           return std::unexpected(target.error());
                                       }
                                       const VriBufferCopyDesc first {half, 0, half};
                                       const VriBufferCopyDesc second {0, half, half};
                                       gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*upload)->handle, &first);
                                       gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*upload)->handle, &second);
                                       return {};
                                   }) ||
            !graph.addPassExpected(names[5],
                                   readLate,
                                   [&](auto* cmd, auto& current) -> std::expected<void, std::string>
                                   {
                                       auto source = current.getBufferExpected(*late);
                                       auto target = current.getBufferExpected(outputs[3]);
                                       if (!source || !target)
                                       {
                                           return std::unexpected("Resolve successor readback resources failed");
                                       }
                                       const VriBufferCopyDesc copy {0, 0, sizeof(words)};
                                       gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*source)->handle, &copy);
                                       return {};
                                   }) ||
            !graph.addOrderingExpected(names[1], names[4]) || !graph.addOrderingExpected(names[2], names[4]) ||
            !graph.addOrderingExpected(names[3], names[4]))
        {
            return std::unexpected("Declare heap successor lifetime failed");
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }
        const auto before    = graph.resourceInfo(*payload);
        const auto successor = graph.resourceInfo(*late);
        if (before.heap == UINT32_MAX || before.heap != successor.heap || before.heapOffset != successor.heapOffset)
        {
            return std::unexpected("In-graph handoff fixture did not reuse native heap bytes");
        }

        std::array<std::unique_ptr<Frame>, 4> frames;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            auto frame = Frame::create(gpu, i == 1 || i == 2 ? VriQueueType_Compute : VriQueueType_Graphics);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            frames[i] = std::move(*frame);
        }
        auto*            a = frames[0]->recordingDestination();
        auto*            b = frames[1]->recordingDestination();
        auto*            c = frames[2]->recordingDestination();
        auto*            d = frames[3]->recordingDestination();
        const std::array recordings {RenderGraph::PassRecording {names[0], a},
                                     RenderGraph::PassRecording {names[1], b},
                                     RenderGraph::PassRecording {names[2], c},
                                     RenderGraph::PassRecording {names[3], d},
                                     RenderGraph::PassRecording {names[4], d},
                                     RenderGraph::PassRecording {names[5], d}};
        const std::array handoffs {Handoff {{*payload, {}, selectedBytes}, names[0], names[1]},
                                   Handoff {{*image, selectedLayer}, names[0], names[1]},
                                   Handoff {{*payload, {}, selectedBytes}, names[2], names[3]},
                                   Handoff {{*image, selectedLayer}, names[2], names[3]}};
        const std::array chain {RenderGraph::RecordingDependency {a, b},
                                RenderGraph::RecordingDependency {b, c},
                                RenderGraph::RecordingDependency {c, d}};
        const std::array missing {chain[0],
                                  RenderGraph::RecordingDependency {a, c},
                                  RenderGraph::RecordingDependency {b, d},
                                  chain[2]};
        auto             between = handoffs;
        between[0].acquireBefore = names[2];
        const auto overlap       = graph.validateOrderedRecordingExpected(a, recordings, chain, {}, between);
        const auto absent        = graph.validateOrderedRecordingExpected(a, recordings, missing, {}, handoffs);
        const auto unowned       = graph.validateOrderedRecordingExpected(a, recordings, chain);
        if (overlap || !overlap.error().contains("overlapping use between") || absent ||
            !absent.error().contains("misses a required submission dependency") || unowned ||
            !unowned.error().contains("exclusive resource range crosses Vulkan queue families"))
        {
            return std::unexpected("In-graph handoff preflight missed an access or ownership dependency");
        }
        if (auto valid = graph.validateOrderedRecordingExpected(a, recordings, chain, {}, handoffs); !valid)
        {
            return valid;
        }
        const auto begin = [&]
        {
            return std::ranges::all_of(frames,
                                       [](const auto& frame)
                                       {
                                           return frame->beginExpected().has_value();
                                       });
        };
        const auto record = [&]
        {
            return graph.executeOrderedExpected(a, recordings, chain, nullptr, {}, handoffs);
        };
        auto buffer  = graph.getBufferExpected(*payload);
        auto texture = graph.getTextureExpected(*image);
        if (!buffer || !texture || !begin())
        {
            return std::unexpected("Begin in-graph handoff failure test failed");
        }
        const auto bufferState = (*buffer)->captureState();
        const auto imageState  = (*texture)->captureState();
        rejectConsumer         = true;
        auto failed            = record();
        if (failed || !failed.error().contains("injected handoff consumer failure") ||
            (*buffer)->captureState() != bufferState || (*texture)->captureState() != imageState ||
            !std::ranges::all_of(frames,
                                 [](const auto& frame)
                                 {
                                     return frame->discardExpected().has_value();
                                 }))
        {
            return std::unexpected("Failed in-graph handoff did not restore state and discard");
        }
        rejectConsumer = false;

        VriFence*  raw     = nullptr;
        const auto created = gpu.core.CreateFence(gpu.handle, 0, &raw);
        const std::unique_ptr<VriFence, decltype(gpu.core.DestroyFence)> fence(raw, gpu.core.DestroyFence);
        if (created != VriResult_Success || !fence)
        {
            return std::unexpected("Create in-graph handoff completion fence failed");
        }
        for (uint64_t cycle = 0; cycle < 2; ++cycle)
        {
            if (!begin())
            {
                return std::unexpected("Begin in-graph handoff retry failed");
            }
            if (auto recorded = record(); !recorded)
            {
                return recorded;
            }
            std::array<VriFenceSubmitDesc, 3> edges;
            for (uint32_t i = 0; i < edges.size(); ++i)
            {
                edges[i] = {fence.get(), cycle * edges.size() + i + 1, VriPipelineStage_AllCommands};
            }
            if (!frames[0]->submitExpected({}, std::span(edges).first(1)) ||
                !frames[1]->submitExpected(std::span(edges).first(1), std::span(edges).subspan(1, 1)) ||
                !frames[2]->submitExpected(std::span(edges).subspan(1, 1), std::span(edges).last(1)) ||
                !frames[3]->submitExpected(std::span(edges).last(1)) ||
                !std::ranges::all_of(frames,
                                     [](const auto& frame)
                                     {
                                         return frame->waitExpected().has_value();
                                     }))
            {
                gpu.waitIdleForDestruction();
                return std::unexpected("Submit in-graph queue-family handoffs failed");
            }
            constexpr std::array<uint32_t, 4> swapped {words[2], words[3], words[0], words[1]};
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                auto output = graph.getBufferExpected(outputs[i]);
                if (!output)
                {
                    return std::unexpected(output.error());
                }
                const auto* data = static_cast<const std::byte*>(gpu.core.MapBuffer((*output)->handle, 0, readSize));
                if (!data)
                {
                    return std::unexpected("Map in-graph handoff readback failed");
                }
                bool exact;
                if (i < 2)
                {
                    exact = std::memcmp(data, words.data() + 2, half) == 0 &&
                            std::memcmp(data + imageOffset, &colors[1], wordBytes) == 0;
                }
                else if (i == 2)
                {
                    exact = std::memcmp(data, words.data(), sizeof(words)) == 0 &&
                            std::memcmp(data + imageOffset, &colors[0], wordBytes) == 0 &&
                            std::memcmp(data + imageOffset + row, &colors[1], wordBytes) == 0;
                }
                else
                {
                    exact = std::memcmp(data, swapped.data(), sizeof(swapped)) == 0;
                }
                gpu.core.UnmapBuffer((*output)->handle);
                if (!exact)
                {
                    return std::unexpected("In-graph handoff or alias activation changed GPU data");
                }
            }
        }
        std::puts("GRAPH_IN_GRAPH_HANDOFF frames=2 words=24 texels=8 alias=1 missing_edge=1 rollback=1");
        return {};
    }

    std::expected<void, std::string> verifyHistoryClearHandoffs(Device& gpu)
    {
        using Use        = RenderGraph::Use;
        using Handoff    = RenderGraph::QueueFamilyHandoff;
        using Reset      = RenderGraph::HistoryResetRecording;
        using Dependency = RenderGraph::RecordingDependency;
        constexpr std::array<uint32_t, 2> words {456, 8123};
        constexpr float                   depthValue   = 0.375f;
        constexpr uint8_t                 stencilValue = 59;
        const auto*                       caps         = gpu.core.GetDeviceDesc(gpu.handle);
        constexpr auto                    format       = VriFormat_D32_SFLOAT_S8_UINT;
        if (!caps->hasSeparateDepthStencilLayouts ||
            !(gpu.core.GetFormatSupport(gpu.handle, format) & VriFormatSupport_DepthStencil))
        {
            std::puts("GRAPH_HISTORY_HANDOFF independent_aspects=unavailable");
            return {};
        }
        const std::string first         = "consume reset depth";
        const std::string last          = "return depth and read stencil";
        const auto        row           = std::max(uint64_t(4), uint64_t(caps->uploadBufferTextureRowAlignment));
        const auto        depthOffset   = std::max(uint64_t(256), row);
        const auto        stencilOffset = depthOffset * 2;
        const auto        readBytes     = stencilOffset + row;
        auto              graphics      = Frame::create(gpu);
        auto              compute       = Frame::create(gpu, VriQueueType_Compute);
        auto              final         = Frame::create(gpu);
        if (!graphics || !compute || !final)
        {
            return std::unexpected("Create history handoff Frames failed");
        }
        auto* clearCommands   = (*graphics)->recordingDestination();
        auto* computeCommands = (*compute)->recordingDestination();
        auto* finalCommands   = (*final)->recordingDestination();

        RenderGraph   graph(gpu);
        VriClearValue initial {};
        initial.depthStencil = {depthValue, stencilValue};
        auto textureDesc     = colorTexture({1, 1}, format);
        textureDesc.usage =
            VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        auto image = graph.createHistoryTextureExpected("packed history", textureDesc, initial);
        auto bytes = graph.createHistoryBufferExpected(
            "byte history",
            {sizeof(words), 0, VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst, VriMemoryLocation_Device},
            std::as_bytes(std::span(words)));
        const VriBufferDesc readDesc {readBytes,
                                      0,
                                      VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                      VriMemoryLocation_HostReadback};
        auto                depthResult   = graph.createBufferExpected("compute depth and bytes", readDesc);
        auto                stencilResult = graph.createBufferExpected("graphics stencil", readDesc);
        if (!image || !bytes || !depthResult || !stencilResult || !graph.exportResourceExpected(*depthResult) ||
            !graph.exportResourceExpected(*stencilResult))
        {
            return std::unexpected("Create history handoff resources failed");
        }

        constexpr RenderGraph::TextureRange depth {.aspect = VriImageAspect_Depth};
        constexpr RenderGraph::TextureRange stencil {.aspect = VriImageAspect_Stencil};
        const auto copyAspect = [&](VriCommandBuffer* cmd, Buffer& target, Texture& source, VriImageAspectFlags aspect)
        {
            const bool                     isDepth = aspect == VriImageAspect_Depth;
            const VriBufferTextureCopyDesc copy {isDepth ? depthOffset : stencilOffset,
                                                 uint32_t(isDepth ? row / sizeof(float) : row),
                                                 1,
                                                 {0, 0, 1, aspect, 0, 0, 0, 1, 1, 1}};
            gpu.core.CmdReadbackTextureToBuffer(cmd, target.handle, source.handle, &copy);
        };
        const std::array firstUses {Use {*image, Usage::eCopySource, false, depth},
                                    Use {*bytes, Usage::eCopySource},
                                    Use {*depthResult, Usage::eCopyDestination, true}};
        const std::array finalUses {Use {*image, Usage::eCopySource},
                                    Use {*bytes, Usage::eCopySource},
                                    Use {*stencilResult, Usage::eCopyDestination, true}};
        bool             rejectRead = false;
        if (!graph.addPassExpected(first,
                                   firstUses,
                                   [&](auto* cmd, auto& active) -> std::expected<void, std::string>
                                   {
                                       if (rejectRead)
                                       {
                                           return std::unexpected("reject cleared history consumer");
                                       }
                                       auto source = active.getTextureExpected(*image);
                                       auto buffer = active.getBufferExpected(*bytes);
                                       auto target = active.getBufferExpected(*depthResult);
                                       if (!source || !buffer || !target)
                                       {
                                           return std::unexpected("Resolve compute history readback failed");
                                       }
                                       const VriBufferCopyDesc copy {0, 0, sizeof(words)};
                                       gpu.core.CmdCopyBuffer(cmd, (*target)->handle, (*buffer)->handle, &copy);
                                       copyAspect(cmd, **target, **source, depth.aspect);
                                       return {};
                                   }) ||
            !graph.addPassExpected(last,
                                   finalUses,
                                   [&](auto* cmd, auto& active) -> std::expected<void, std::string>
                                   {
                                       auto source = active.getTextureExpected(*image);
                                       auto target = active.getBufferExpected(*stencilResult);
                                       if (!source || !target)
                                       {
                                           return std::unexpected("Resolve graphics stencil readback failed");
                                       }
                                       copyAspect(cmd, **target, **source, stencil.aspect);
                                       return {};
                                   }) ||
            !graph.compileHeapsExpected())
        {
            return std::unexpected("Compile history handoff graph failed");
        }

        const std::array recordings {RenderGraph::PassRecording {first, computeCommands},
                                     RenderGraph::PassRecording {last, finalCommands}};
        const std::array order {Dependency {clearCommands, computeCommands},
                                Dependency {computeCommands, finalCommands}};
        const std::array placements {Reset {{*image}, graphics->get()},
                                     Reset {{*bytes, {}, {0, sizeof(uint32_t)}}, graphics->get()},
                                     Reset {{*bytes, {}, {sizeof(uint32_t), sizeof(uint32_t)}}, graphics->get()}};
        const std::array handoffs {Handoff {{*image, depth}, {}, first, graphics->get()},
                                   Handoff {{*bytes}, {}, first, graphics->get()},
                                   Handoff {{*image, depth}, first, last},
                                   Handoff {{*bytes}, first, last}};
        const auto       validate = [&](auto edges, auto resets, auto transfers)
        {
            return graph.validateOrderedRecordingExpected(computeCommands, recordings, edges, resets, transfers);
        };
        const auto incomplete        = validate(order, std::span(placements).first(2), handoffs);
        const auto unordered         = validate(std::span(order).last(1), placements, handoffs);
        auto       invalid           = handoffs;
        invalid.front().releaseAfter = first;
        const auto ambiguous         = validate(order, placements, invalid);
        if (incomplete || !incomplete.error().contains("release Frame must clear the full handoff range") ||
            unordered || !unordered.error().contains("misses a required submission dependency") || ambiguous ||
            !ambiguous.error().contains("requires one release pass or history clear Frame") ||
            !validate(order, placements, handoffs))
        {
            return std::unexpected("History handoff accepted invalid preflight or rejected a complete placement");
        }
        const auto begin = [&]
        {
            return (*graphics)->beginExpected() && (*compute)->beginExpected() && (*final)->beginExpected();
        };
        const auto record = [&]
        {
            return graph.executeOrderedExpected(computeCommands, recordings, order, nullptr, placements, handoffs);
        };
        rejectRead = true;
        if (!begin())
        {
            return std::unexpected("Begin history handoff failure control failed");
        }
        const auto rejected = record();
        if (rejected || !rejected.error().contains("reject cleared history consumer") ||
            !graph.resourceInfo(*image).historyResetPending || !graph.resourceInfo(*bytes).historyResetPending ||
            !(*graphics)->discardExpected() || !(*compute)->discardExpected() || !(*final)->discardExpected())
        {
            return std::unexpected("Failed history handoff lost its pending clears or Frame recordings");
        }
        rejectRead         = false;
        VriFence*  raw     = nullptr;
        const auto created = gpu.core.CreateFence(gpu.handle, 0, &raw);
        const std::unique_ptr<VriFence, decltype(gpu.core.DestroyFence)> fence(raw, gpu.core.DestroyFence);
        if (created != VriResult_Success || !fence)
        {
            return std::unexpected("Create history handoff fence failed");
        }
        for (uint64_t cycle = 0; cycle < 2; ++cycle)
        {
            if (cycle)
            {
                const std::array resets {RenderGraph::HistorySelection {*image},
                                         RenderGraph::HistorySelection {*bytes}};
                if (auto reset = graph.resetHistoryExpected(resets); !reset)
                {
                    return reset;
                }
            }
            if (!begin())
            {
                return std::unexpected("Begin history handoff replay failed");
            }
            if (auto result = record(); !result)
            {
                return result;
            }
            const std::array ready {VriFenceSubmitDesc {fence.get(), 2 * cycle + 1, VriPipelineStage_AllCommands}};
            const std::array consumed {VriFenceSubmitDesc {fence.get(), 2 * cycle + 2, VriPipelineStage_AllCommands}};
            if (!(*graphics)->submitExpected({}, ready) || !(*compute)->submitExpected(ready, consumed) ||
                !(*final)->submitExpected(consumed) || !(*final)->waitExpected() || !(*compute)->waitExpected() ||
                !(*graphics)->waitExpected())
            {
                gpu.waitIdleForDestruction();
                return std::unexpected("Submit history clear, acquire and return failed");
            }
            const auto verify = [&](RenderGraph::Resource resource, bool isDepth)
            {
                auto buffer = graph.getBufferExpected(resource);
                if (!buffer)
                {
                    return false;
                }
                const auto* data = static_cast<const std::byte*>(gpu.core.MapBuffer((*buffer)->handle, 0, readBytes));
                if (!data)
                {
                    return false;
                }
                const bool exact = isDepth ?
                                       std::memcmp(data, words.data(), sizeof(words)) == 0 &&
                                           std::memcmp(data + depthOffset, &depthValue, sizeof(depthValue)) == 0 :
                                       std::memcmp(data + stencilOffset, &stencilValue, sizeof(stencilValue)) == 0;
                gpu.core.UnmapBuffer((*buffer)->handle);
                return exact;
            };
            if (!verify(*depthResult, true) || !verify(*stencilResult, false))
            {
                return std::unexpected("History handoff corrupted reset bytes, depth or the retained stencil aspect");
            }
            const auto stale = validate(order, placements, handoffs);
            if (stale || !stale.error().contains("release Frame must clear the full handoff range"))
            {
                return std::unexpected("History handoff reused a release source with no pending clear");
            }
        }
        std::puts("GRAPH_HISTORY_HANDOFF frames=2 words=4 depth=2 stencil=2 byte_union=1 rollback=1");
        // Leave every used range with compute across a completed frame.
        const std::array computeRecordings {RenderGraph::PassRecording {first, computeCommands},
                                            RenderGraph::PassRecording {last, computeCommands}};
        const std::array computeOrder {Dependency {clearCommands, computeCommands}};
        const std::array initialization {handoffs[0],
                                         handoffs[1],
                                         Handoff {{*image, stencil}, {}, last, graphics->get()}};
        const std::array allHistory {RenderGraph::HistorySelection {*image}, RenderGraph::HistorySelection {*bytes}};
        if (!graph.resetHistoryExpected(allHistory))
        {
            return std::unexpected("Reset the cross-frame ownership fixture failed");
        }
        auto release = Frame::create(gpu, VriQueueType_Compute);
        if (!release)
        {
            return std::unexpected(release.error());
        }
        auto*                               releaseCommands = (*release)->recordingDestination();
        const RenderGraph::HistorySelection resetDepth {*image, depth};
        const RenderGraph::HistorySelection resetWord {*bytes, {}, {0, sizeof(uint32_t)}};
        const std::array                    partial {resetDepth, resetWord};
        const std::array                    movingClears {Reset {{*image}, graphics->get(), release->get()},
                                       Reset {{*bytes}, graphics->get(), release->get()}};
        const std::array                    movingOrder {Dependency {releaseCommands, clearCommands}, computeOrder[0]};
        const std::array movingHandoffs {handoffs[0], Handoff {resetWord, {}, first, graphics->get()}};
        const auto       wrongOwner = graph.validateOrderedRecordingExpected(clearCommands,
                                                                       computeRecordings,
                                                                       movingOrder,
                                                                       movingClears,
                                                                       movingHandoffs);
        if (wrongOwner || !wrongOwner.error().contains("history range is not owned by the releasing queue family"))
        {
            return std::unexpected("History clear accepted a release from a non-owner family");
        }
        if (!(*graphics)->beginExpected() || !(*compute)->beginExpected())
        {
            return std::unexpected("Begin cross-frame ownership fixture failed");
        }
        if (auto result = graph.executeOrderedExpected(clearCommands,
                                                       computeRecordings,
                                                       computeOrder,
                                                       nullptr,
                                                       placements,
                                                       initialization);
            !result)
        {
            return result;
        }
        const std::array initialized {VriFenceSubmitDesc {fence.get(), 5, VriPipelineStage_AllCommands}};
        if (!(*graphics)->submitExpected({}, initialized) || !(*compute)->submitExpected(initialized) ||
            !(*compute)->waitExpected() || !(*graphics)->waitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit cross-frame history initialization failed");
        }

        // Change both words on the current owning queue before resetting only the first word.
        constexpr std::array<uint32_t, 2> changed {987u, 654u};
        auto                              upload =
            Buffer::create(gpu, {sizeof(changed), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto history = graph.getBufferExpected(*bytes);
        if (!upload || !history)
        {
            return std::unexpected("Resolve cross-frame history buffer and upload failed");
        }
        auto* destination = gpu.core.MapBuffer((*upload)->handle, 0, sizeof(changed));
        if (!destination)
        {
            return std::unexpected("Map cross-frame history upload failed");
        }
        std::memcpy(destination, changed.data(), sizeof(changed));
        gpu.core.UnmapBuffer((*upload)->handle);
        (*upload)->setState({VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        if (!(*compute)->beginExpected() ||
            !(*history)->transitionExpected(computeCommands,
                                            {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer}))
        {
            return std::unexpected("Prepare cross-frame history update failed");
        }
        const VriBufferCopyDesc update {0, 0, sizeof(changed)};
        gpu.core.CmdCopyBuffer(computeCommands, (*history)->handle, (*upload)->handle, &update);
        if (!(*compute)->submitAndWaitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit cross-frame history update failed");
        }
        if (auto reset = graph.resetHistoryExpected(partial); !reset)
        {
            return reset;
        }

        const auto validateMoving = [&](auto edges, auto clears)
        {
            return graph.validateOrderedRecordingExpected(clearCommands,
                                                          computeRecordings,
                                                          edges,
                                                          clears,
                                                          movingHandoffs);
        };
        auto unownedClears              = movingClears;
        unownedClears.front().release   = nullptr;
        const auto       missingRelease = validateMoving(movingOrder, unownedClears);
        const auto       missingOrder   = validateMoving(computeOrder, movingClears);
        const std::array cyclic {movingOrder[0], movingOrder[1], Dependency {computeCommands, releaseCommands}};
        if (missingRelease ||
            !missingRelease.error().contains("exclusive resource range crosses Vulkan queue families") ||
            missingOrder || !missingOrder.error().contains("misses a required submission dependency") ||
            validateMoving(cyclic, movingClears) || !validateMoving(movingOrder, movingClears))
        {
            return std::unexpected("History acquisition accepted missing ownership or invalid queue order");
        }

        const auto recordMoving = [&]
        {
            return graph.executeOrderedExpected(clearCommands,
                                                computeRecordings,
                                                movingOrder,
                                                nullptr,
                                                movingClears,
                                                movingHandoffs);
        };
        const auto beginMoving = [&]
        {
            return (*release)->beginExpected() && (*graphics)->beginExpected() && (*compute)->beginExpected();
        };
        if (!beginMoving())
        {
            return std::unexpected("Begin cross-frame history transaction failed");
        }
        {
            const std::array<const Frame*, 3> guarded {release->get(), graphics->get(), compute->get()};
            auto                              checkpoint = graph.checkpointExpected(guarded);
            auto                              incomplete = graph.checkpointExpected(std::span(guarded).last(2));
            if (!checkpoint || !incomplete || !recordMoving() || !(*graphics)->discardExpected() ||
                !(*compute)->discardExpected())
            {
                return std::unexpected("History rollback did not require discarding the release-only Frame");
            }
            const auto omitted = graph.restoreUnsubmittedExpected(std::move(*incomplete));
            if (omitted || !omitted.error().contains("omits a recorded command buffer") ||
                graph.restoreUnsubmittedExpected(std::move(*checkpoint)) || !(*release)->discardExpected())
            {
                return std::unexpected("A release-only destination was not covered by the guarded checkpoint");
            }
            if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
            {
                return restored;
            }
        }
        if (!graph.resourceInfo(*image).historyResetPending || !graph.resourceInfo(*bytes).historyResetPending ||
            !beginMoving())
        {
            return std::unexpected("Restored history did not retain the pending reset");
        }
        if (auto result = recordMoving(); !result)
        {
            return result;
        }
        const std::array released {VriFenceSubmitDesc {fence.get(), 6, VriPipelineStage_AllCommands}};
        const std::array cleared {VriFenceSubmitDesc {fence.get(), 7, VriPipelineStage_AllCommands}};
        if (!(*release)->submitExpected({}, released) || !(*graphics)->submitExpected(released, cleared) ||
            !(*compute)->submitExpected(cleared) || !(*compute)->waitExpected() || !(*graphics)->waitExpected() ||
            !(*release)->waitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit cross-frame release, clear and acquire failed");
        }
        auto result          = graph.getBufferExpected(*depthResult);
        auto stencilReadback = graph.getBufferExpected(*stencilResult);
        if (!result || !stencilReadback)
        {
            return std::unexpected("Resolve cross-frame readbacks failed");
        }
        const std::array expected {words.front(), changed.back()};
        const auto*      readback = static_cast<const std::byte*>(gpu.core.MapBuffer((*result)->handle, 0, readBytes));
        if (!readback)
        {
            return std::unexpected("Map cross-frame history readback failed");
        }
        const bool exact = std::memcmp(readback, expected.data(), sizeof(expected)) == 0 &&
                           std::memcmp(readback + depthOffset, &depthValue, sizeof(depthValue)) == 0;
        gpu.core.UnmapBuffer((*result)->handle);
        const auto* stencilBytes =
            static_cast<const std::byte*>(gpu.core.MapBuffer((*stencilReadback)->handle, 0, readBytes));
        if (!stencilBytes)
        {
            return std::unexpected("Map untouched stencil history failed");
        }
        const bool keptStencil = std::memcmp(stencilBytes + stencilOffset, &stencilValue, sizeof(stencilValue)) == 0;
        gpu.core.UnmapBuffer((*stencilReadback)->handle);
        if (!exact || !keptStencil)
        {
            return std::unexpected("Cross-frame history acquisition did not preserve the adjacent ranges");
        }
        if (!graph.validateOrderedRecordingExpected(clearCommands, computeRecordings, movingOrder, movingClears))
        {
            return std::unexpected("Completed history clear did not leave the retained ranges on compute");
        }
        std::puts(
            "GRAPH_HISTORY_ACQUIRE family_roundtrip=1 selected_word=1 retained_word=1 depth=1 stencil=1 rollback=1");

        return {};
    }

    std::expected<void, std::string> verifyVulkanQueueFamilies(Device& original)
    {
        if (!original.core.GetCommandBufferQueueFamilyIndex)
        {
            return {};
        }
        auto optedIn = Device::create(true, nullptr, 0, false, true);
        if (!optedIn)
        {
            return std::unexpected(optedIn.error());
        }
        auto& gpu      = **optedIn;
        auto  graphics = Frame::create(gpu);
        auto  compute  = Frame::create(gpu, VriQueueType_Compute);
        if (!graphics || !compute)
        {
            return std::unexpected("Create Vulkan queue-family fixture frames failed");
        }
        auto a = (*graphics)->beginExpected();
        auto b = (*compute)->beginExpected();
        if (!a || !b)
        {
            return std::unexpected("Begin Vulkan queue-family fixture failed");
        }
        const auto     query          = gpu.core.GetCommandBufferQueueFamilyIndex;
        const uint32_t graphicsFamily = query(gpu.handle, *a);
        const uint32_t computeFamily  = query(gpu.handle, *b);
        if (graphicsFamily == UINT32_MAX || computeFamily == UINT32_MAX || query(original.handle, *a) != UINT32_MAX)
        {
            return std::unexpected("Vulkan command buffer family or device identity is invalid");
        }
        if (graphicsFamily == computeFamily)
        {
            std::puts("GRAPH_VULKAN_FAMILIES separate=0");
            return {};
        }
        VriQueue*                graphicsQueue = nullptr;
        const VriQueueSubmitDesc wrongQueue {.commandBuffers = &*b, .commandBufferNum = 1};
        if (gpu.core.GetQueue(gpu.handle, VriQueueType_Graphics, 0, &graphicsQueue) != VriResult_Success ||
            gpu.core.EndCommandBuffer(*b) != VriResult_Success ||
            gpu.core.QueueSubmit(graphicsQueue, &wrongQueue) != VriResult_InvalidArgument ||
            !(*compute)->discardExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Vulkan accepted a command buffer from the wrong queue family");
        }
        b = (*compute)->beginExpected();
        if (!b)
        {
            return std::unexpected(b.error());
        }
        using Use = RenderGraph::Use;
        const std::array    order {RenderGraph::RecordingDependency {*a, *b}};
        constexpr uint64_t  bytes = 4 * sizeof(uint32_t);
        const VriBufferDesc deviceDesc {bytes * 2,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                        VriMemoryLocation_Device};
        for (bool texture : {false, true})
        {
            for (bool history : {false, true})
            {
                RenderGraph rejected(gpu);
                auto        textureDesc = colorTexture({8, 8});
                textureDesc.usage |=
                    VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst | VriTextureUsage_ShaderResourceStorage;
                std::expected<RenderGraph::Resource, std::string> resource;
                if (texture)
                {
                    resource = history ? rejected.createHistoryTextureExpected("exclusive", textureDesc, {}) :
                                         rejected.createTextureExpected("exclusive", textureDesc);
                }
                else
                {
                    resource = history ? rejected.createHistoryBufferExpected("exclusive", deviceDesc) :
                                         rejected.createBufferExpected("exclusive", deviceDesc);
                }
                if (!resource)
                {
                    return std::unexpected(resource.error());
                }
                unsigned   callbacks = 0;
                const auto count     = [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    ++callbacks;
                    return {};
                };
                const std::array write {Use {*resource, Usage::eCopyDestination, true}};
                const std::array read {Use {*resource, Usage::eCopySource}};
                if ((!history && !rejected.addPassExpected("write", write, count)) ||
                    !rejected.addPassExpected("read", read, count, true) || !rejected.compileExpected())
                {
                    return std::unexpected("Compile exclusive Vulkan queue-family fixture failed");
                }
                const std::array recordings {RenderGraph::PassRecording {"write", *a},
                                             RenderGraph::PassRecording {"read", *b}};
                const auto       record =
                    rejected.executeOrderedExpected(*a, std::span(recordings).last(history ? 1 : 2), order);
                if (record || callbacks ||
                    record.error().find("read: exclusive resource range crosses Vulkan queue families: exclusive") ==
                        std::string::npos)
                {
                    return std::unexpected("Exclusive Vulkan range reached a routed callback");
                }
            }
        }

        RenderGraph graph(gpu);
        auto        upload = Buffer::create(
            gpu,
            {bytes, 0, VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostUpload});
        auto output = Buffer::create(gpu, {bytes * 2, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!upload || !output)
        {
            return std::unexpected("Create Vulkan queue-family staging failed");
        }
        constexpr std::array<uint32_t, 4> pattern {0x1a3b5c7d, 91, 0xfe770012, 0x9b8f542a};
        auto*                             data = gpu.core.MapBuffer((*upload)->handle, 0, bytes);
        if (!data)
        {
            return std::unexpected("Map Vulkan queue-family upload failed");
        }
        std::memcpy(data, pattern.data(), bytes);
        gpu.core.UnmapBuffer((*upload)->handle);
        auto source     = graph.importResourceExpected("upload", **upload);
        auto result     = graph.importResourceExpected("readback", **output, false);
        auto sharedDesc = deviceDesc;
        sharedDesc.usage |= VriBufferUsage_ConcurrentQueues;
        auto shared   = graph.createBufferExpected("shared", sharedDesc);
        auto disjoint = graph.createBufferExpected("disjoint", deviceDesc);
        if (!source || !result || !shared || !disjoint)
        {
            return std::unexpected("Create Vulkan queue-family graph resources failed");
        }
        const RenderGraph::BufferRange first {0, bytes};
        const RenderGraph::BufferRange second {bytes, bytes};
        const std::array               write {Use {*source, Usage::eCopySource},
                                Use {*shared, Usage::eCopyDestination, true, {}, first},
                                Use {*disjoint, Usage::eCopyDestination, true, {}, first}};
        const std::array               writeSecond {Use {*source, Usage::eCopySource},
                                      Use {*disjoint, Usage::eCopyDestination, true, {}, second}};
        const std::array               read {Use {*shared, Usage::eCopySource, false, {}, first},
                               Use {*disjoint, Usage::eCopySource, false, {}, second},
                               Use {*result, Usage::eCopyDestination, true}};
        const auto                     copy = [&](VriCommandBuffer*     commands,
                              RenderGraph::Resource dst,
                              RenderGraph::Resource src,
                              VriBufferCopyDesc     range) -> std::expected<void, std::string>
        {
            auto input  = graph.getBufferExpected(src);
            auto target = graph.getBufferExpected(dst);
            if (!input || !target)
            {
                return std::unexpected(!input ? input.error() : target.error());
            }
            gpu.core.CmdCopyBuffer(commands, (*target)->handle, (*input)->handle, &range);
            return {};
        };
        if (!graph.addPassExpected("graphics write",
                                   write,
                                   [&](auto* commands, auto&) -> std::expected<void, std::string>
                                   {
                                       if (auto copied = copy(commands, *shared, *source, {0, 0, bytes}); !copied)
                                       {
                                           return copied;
                                       }
                                       return copy(commands, *disjoint, *source, {0, 0, bytes});
                                   }) ||
            !graph.addPassExpected("compute write",
                                   writeSecond,
                                   [&](auto* commands, auto&)
                                   {
                                       return copy(commands, *disjoint, *source, {0, bytes, bytes});
                                   }) ||
            !graph.addPassExpected("compute read",
                                   read,
                                   [&](auto* commands, auto&) -> std::expected<void, std::string>
                                   {
                                       if (auto copied = copy(commands, *result, *shared, {0, 0, bytes}); !copied)
                                       {
                                           return copied;
                                       }
                                       return copy(commands, *result, *disjoint, {bytes, bytes, bytes});
                                   }) ||
            !graph.exportResourceExpected(*result) || !graph.compileExpected())
        {
            return std::unexpected("Compile shared Vulkan queue-family fixture failed");
        }
        const std::array recordings {RenderGraph::PassRecording {"graphics write", *a},
                                     RenderGraph::PassRecording {"compute write", *b},
                                     RenderGraph::PassRecording {"compute read", *b}};
        if (auto recorded = graph.executeOrderedExpected(*a, recordings, order); !recorded)
        {
            return recorded;
        }
        VriFence*  rawFence = nullptr;
        const auto created  = gpu.core.CreateFence(gpu.handle, 0, &rawFence);
        const std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(rawFence,
                                                                                        gpu.core.DestroyFence);
        if (created != VriResult_Success || !fence)
        {
            return std::unexpected("Create Vulkan queue-family fixture fence failed");
        }
        const std::array dependency {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands}};
        if (!(*graphics)->submitExpected({}, dependency) || !(*compute)->submitExpected(dependency) ||
            !(*compute)->waitExpected() || !(*graphics)->waitExpected())
        {
            gpu.waitIdleForDestruction();
            return std::unexpected("Submit Vulkan queue-family fixture failed");
        }
        const auto* mapped = static_cast<const std::byte*>(gpu.core.MapBuffer((*output)->handle, 0, bytes * 2));
        if (!mapped)
        {
            return std::unexpected("Map Vulkan queue-family readback failed");
        }
        const bool exact =
            std::memcmp(mapped, pattern.data(), bytes) == 0 && std::memcmp(mapped + bytes, pattern.data(), bytes) == 0;
        gpu.core.UnmapBuffer((*output)->handle);
        if (!exact)
        {
            return std::unexpected("Vulkan queue-family readback changed the copied words");
        }
        std::printf("GRAPH_VULKAN_FAMILIES graphics=%u compute=%u rejected=4 words=8\n", graphicsFamily, computeFamily);
        if (auto lifetime = verifyVulkanFamilyLifetime(gpu, **graphics, **compute); !lifetime)
        {
            return lifetime;
        }
        if (auto tracked = verifyTrackedImports(gpu, **graphics, **compute); !tracked)
        {
            return tracked;
        }
        if (auto transferred = verifyVulkanTransfers(gpu, **graphics, **compute, false); !transferred)
        {
            return transferred;
        }
        if (auto transferred = verifyVulkanTransfers(gpu, **graphics, **compute, true); !transferred)
        {
            return transferred;
        }
        if (auto routed = verifyRoutedHistoryClears(gpu); !routed)
        {
            return routed;
        }
        if (auto handoffs = verifyInGraphHandoffs(gpu); !handoffs)
        {
            return handoffs;
        }
        return verifyHistoryClearHandoffs(gpu);
    }

    std::expected<void, std::string> run()
    {
        auto device = Device::create(true);
        if (!device)
            return std::unexpected(device.error());
        auto& gpu = **device;
        if (auto families = verifyVulkanQueueFamilies(gpu); !families)
        {
            return families;
        }
        if (gpu.core.GetDeviceDesc(gpu.handle)->graphicsAPI == VriGraphicsAPI_Metal)
        {
            if (auto clears = verifyRoutedHistoryClears(gpu); !clears)
            {
                return clears;
            }
        }
        if (auto guards = verifyGuardedCheckpointOrder(gpu); !guards)
        {
            return guards;
        }
        if (auto reset = verifyCheckedFrameReset(gpu); !reset)
        {
            return reset;
        }
        if (auto state = verifyPassState(gpu); !state)
        {
            return state;
        }
        if (auto transaction = verifyStateTransaction(gpu); !transaction)
        {
            return transaction;
        }
        if (auto aspects = verifyAspectDependencies(gpu); !aspects)
        {
            return aspects;
        }
        if (auto routed = verifyRecordingDestinations(gpu); !routed)
        {
            return routed;
        }
        if (auto rebound = verifyBindings(gpu); !rebound)
        {
            return rebound;
        }
        if (auto mixed = verifyMixedGraphState(gpu); !mixed)
        {
            return mixed;
        }
        auto uniform = Buffer::create(gpu, {256, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        if (!uniform)
            return std::unexpected(uniform.error());
        RenderGraph graph(gpu), foreign(gpu);
        auto        constant = graph.importResourceExpected("constant", **uniform);
        if (!constant)
            return std::unexpected(constant.error());
        auto resource = graph.createTextureExpected("color", colorTexture({8, 8}));
        auto other    = foreign.createTextureExpected("foreign", colorTexture({8, 8}));
        if (!resource || !other)
            return std::unexpected("Create graph resource failed");
        const auto found   = graph.findResourceExpected("color");
        const auto missing = graph.findResourceExpected("missing");
        if (!found || found->graph != &graph || found->index != resource->index || missing ||
            missing.error().find("Unknown graph resource") == std::string::npos)
            return std::unexpected("Expected graph lookup did not preserve identity or reject a missing name");
        RenderGraph ambiguous(gpu);
        if (!ambiguous.createTextureExpected("duplicate", colorTexture({8, 8})) ||
            !ambiguous.createTextureExpected("duplicate", colorTexture({8, 8})))
            return std::unexpected("Create duplicate-name resources failed");
        const auto duplicate = ambiguous.findResourceExpected("duplicate");
        if (duplicate || duplicate.error().find("Ambiguous graph resource") == std::string::npos)
            return std::unexpected("Expected graph lookup accepted an ambiguous name");
        const std::array badUses {RenderGraph::Use {*other, Usage::eColorWrite}};
        if (graph.addPassExpected("foreign",
                                  badUses,
                                  [](auto*, auto&) -> std::expected<void, std::string>
                                  {
                                      return {};
                                  }) ||
            graph.exportResourceExpected(*other) || graph.getTextureExpected(*other))
            return std::unexpected("Graph accepted a foreign resource");
        const std::array write {RenderGraph::Use {*resource, Usage::eColorWrite}};
        auto             clear = graph.addPassExpected("clear",
                                           write,
                                           [resource](auto* cmd, auto& g) -> std::expected<void, std::string>
                                           {
                                               auto texture = g.getTextureExpected(*resource);
                                               if (!texture)
                                                   return std::unexpected(texture.error());
                                               auto view = (*texture)->viewExpected();
                                               if (!view)
                                                   return std::unexpected(view.error());
                                               const float color[] {1, 0, 0, 1};
                                               beginColorPass(g.device(), cmd, *view, {8, 8}, color);
                                               g.device().core.CmdEndRendering(cmd);
                                               return {};
                                           });
        bool             fail  = true;
        const std::array read {RenderGraph::Use {*resource, Usage::eSampled},
                               RenderGraph::Use {*constant, Usage::eConstantRead}};
        auto             inspect = graph.addPassExpected(
            "inspect",
            read,
            [&fail](auto*, auto&) -> std::expected<void, std::string>
            {
                if (fail)
                    return std::unexpected("injected failure");
                return {};
            },
            true);
        if (!clear || !inspect || !graph.exportResourceExpected(*resource) || !graph.compileExpected())
            return std::unexpected("Compile expected graph failed");
        auto texture = graph.getTextureExpected(*resource);
        auto frame   = Frame::create(gpu);
        if (!texture || !frame)
            return std::unexpected("Prepare expected graph failed");
        const auto initial         = (*texture)->captureState();
        const auto initialConstant = (*uniform)->captureState();
        auto       cmd             = (*frame)->beginExpected();
        if (!cmd)
            return std::unexpected(cmd.error());
        auto failed = graph.executeExpected(*cmd);
        if (failed || failed.error().find("inspect: injected failure") == std::string::npos ||
            (*texture)->captureState() != initial || (*uniform)->captureState() != initialConstant)
        {
            return std::unexpected("Graph did not propagate failure and restore tracked state");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        fail = false;
        cmd  = (*frame)->beginExpected();
        if (!cmd)
            return std::unexpected(cmd.error());
        if (auto executed = graph.executeExpected(*cmd); !executed)
            return executed;
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            return submitted;
        auto constantState = (*uniform)->stateExpected();
        if (!constantState || constantState->access != VriAccess_ConstantBufferRead)
        {
            return std::unexpected("Constant buffer did not transition to constant read");
        }

        auto mipDesc    = colorTexture({8, 8});
        mipDesc.mipNum  = 4;
        auto mipTexture = Texture::create(gpu, mipDesc);
        if (!mipTexture)
            return std::unexpected(mipTexture.error());
        RenderGraph mipGraph(gpu);
        auto        mipResource = mipGraph.importResourceExpected("mip texture", **mipTexture);
        if (!mipResource)
            return std::unexpected(mipResource.error());
        const std::array mipRead {RenderGraph::Use {*mipResource, Usage::eSampled}};
        if (!mipGraph.addPassExpected(
                "sample mips",
                mipRead,
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true) ||
            !mipGraph.compileExpected())
            return std::unexpected("Graph rejected imported multi-mip sampling");
        RenderGraph mipWriteGraph(gpu);
        auto        mipWriteResource = mipWriteGraph.importResourceExpected("mip texture", **mipTexture);
        if (!mipWriteResource)
            return std::unexpected(mipWriteResource.error());
        const std::array mipWrite {RenderGraph::Use {*mipWriteResource, Usage::eColorWrite}};
        if (!mipWriteGraph.addPassExpected(
                "write mip",
                mipWrite,
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true))
            return std::unexpected("Cannot create invalid multi-mip write test");
        const auto compiledMipWrite = mipWriteGraph.compileExpected();
        if (!compiledMipWrite || !mipWriteGraph.createTextureExpected("transient mips", mipDesc))
        {
            return std::unexpected("Graph rejected multi-mip writes or allocation");
        }

        auto checkpoint = graph.checkpointExpected();
        if (!checkpoint)
            return std::unexpected(checkpoint.error());
        const auto beforeDiscard = (*texture)->captureState();
        cmd                      = (*frame)->beginExpected();
        if (!cmd)
            return std::unexpected(cmd.error());
        if (auto executed = graph.executeExpected(*cmd); !executed)
            return executed;
        (*texture)->transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        if (foreign.restoreUnsubmittedExpected(std::move(*checkpoint)))
            return std::unexpected("Another graph accepted the checkpoint");
        if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
            return restored;
        if ((*texture)->captureState() != beforeDiscard || graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
        {
            return std::unexpected("Discarded graph state was not restored or checkpoint was reused");
        }
        cmd = (*frame)->beginExpected();
        if (!cmd)
            return std::unexpected(cmd.error());
        if (auto executed = graph.executeExpected(*cmd); !executed)
            return executed;
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            return submitted;

        RenderGraph wrongType(gpu);
        auto        wrongTexture = wrongType.createTextureExpected("wrong", colorTexture({4, 4}));
        if (!wrongTexture)
            return std::unexpected(wrongTexture.error());
        const std::array wrongUse {RenderGraph::Use {*wrongTexture, Usage::eConstantRead}};
        if (!wrongType.addPassExpected(
                "wrong",
                wrongUse,
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true))
            return std::unexpected("Cannot create invalid constant-texture test");
        const auto wrongTextureResult = wrongType.compileExpected();
        if (wrongTextureResult || wrongTextureResult.error().find("Unsupported graph usage") == std::string::npos)
            return std::unexpected("Graph did not reject constant access on a texture");
        RenderGraph wrongFlags(gpu);
        auto        wrongBuffer =
            wrongFlags.createBufferExpected("wrong", {256, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device});
        if (!wrongBuffer)
            return std::unexpected(wrongBuffer.error());
        const std::array wrongBufferUse {RenderGraph::Use {*wrongBuffer, Usage::eConstantRead}};
        if (!wrongFlags.addPassExpected(
                "wrong",
                wrongBufferUse,
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                true))
            return std::unexpected("Cannot create invalid constant-buffer test");
        const auto wrongBufferResult = wrongFlags.compileExpected();
        if (wrongBufferResult || wrongBufferResult.error().find("lacks required usage") == std::string::npos)
            return std::unexpected("Graph did not reject a buffer without constant usage");

        RenderGraph allocations(gpu);
        auto        first  = allocations.createTextureExpected("first", colorTexture({4, 4}));
        auto        second = allocations.createTextureExpected("second", colorTexture({4, 4}));
        if (!first || !second)
            return std::unexpected("Create allocation test resources failed");
        const auto noop = [](auto*, auto&) -> std::expected<void, std::string>
        {
            return {};
        };
        const std::array firstWrite {RenderGraph::Use {*first, Usage::eColorWrite}};
        const std::array secondWrite {RenderGraph::Use {*second, Usage::eColorWrite}};
        if (!allocations.addPassExpected("first", firstWrite, noop) ||
            !allocations.addPassExpected("second", secondWrite, noop) || !allocations.exportResourceExpected(*first) ||
            !allocations.exportResourceExpected(*second))
            return std::unexpected("Prepare allocation failure graph failed");
        const auto core        = gpu.core;
        createTexture          = core.CreateTexture;
        textureCreates         = 0;
        gpu.core.CreateTexture = [](VriDevice* d, const VriTextureDesc* desc, VriTexture** out)
        {
            if (++textureCreates == 2)
                return VriResult_DeviceLost;
            return createTexture(d, desc, out);
        };
        const auto failedAllocation = allocations.compileExpected(true);
        gpu.core                    = core;
        if (failedAllocation || textureCreates != 2 || !allocations.compileExpected(true))
            return std::unexpected("Graph allocation failure could not be retried");
        if (allocations.createBufferExpected("late", {16, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device}))
            return std::unexpected("Aliased graph accepted a new buffer");
        RenderGraph buffers(gpu);
        auto        buffer =
            buffers.createBufferExpected("payload", {16, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device});
        if (!buffer)
            return std::unexpected(buffer.error());
        const std::array bufferWrite {RenderGraph::Use {*buffer, Usage::eStorageWrite, true}};
        if (!buffers.addPassExpected("write payload", bufferWrite, noop) || !buffers.exportResourceExpected(*buffer) ||
            !buffers.compileExpected() || !buffers.getBufferExpected(*buffer))
            return std::unexpected("Expected graph buffer allocation failed");
        return {};
    }
} // namespace

int main()
{
    auto result = run();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Expected graph failure propagation, state rollback and allocation recovery passed");
    return 0;
}
