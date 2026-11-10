#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace vultra::test::buffer_ranges_probe
{
    using Inspect = std::expected<void, std::string> (*)(const RenderGraph&);
    using Fence   = std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)>;

    inline VriBufferDesc description(uint64_t bytes, VriMemoryLocation location = VriMemoryLocation_Device)
    {
        return {bytes,
                0,
                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                location};
    }

    inline RenderGraph::Use
    use(RenderGraph::Resource resource, Usage usage, uint64_t offset, uint64_t size, bool discard = false)
    {
        return {resource, usage, discard, {}, {offset, size}};
    }

    inline std::expected<void, std::string> copy(Device&               device,
                                                 VriCommandBuffer*     commands,
                                                 RenderGraph&          graph,
                                                 RenderGraph::Resource source,
                                                 RenderGraph::Resource destination,
                                                 uint64_t              sourceOffset,
                                                 uint64_t              destinationOffset,
                                                 uint64_t              size)
    {
        auto from = graph.getBufferExpected(source);
        auto to   = graph.getBufferExpected(destination);
        if (!from || !to)
        {
            return std::unexpected("Byte-range copy resources are unavailable");
        }
        VriBufferCopyDesc region {};
        region.srcOffset = sourceOffset;
        region.dstOffset = destinationOffset;
        region.size      = size;
        device.core.CmdCopyBuffer(commands, (*to)->handle, (*from)->handle, &region);
        return {};
    }

    struct BarrierFault
    {
        Device&                                           device;
        decltype(VriCoreInterface::CmdBufferBarrierRange) original;
        decltype(VriCoreInterface::CmdBarrier)             originalGroup;
        unsigned                                          reject;
        std::vector<VriBufferBarrierDesc>                 barriers;
        inline static BarrierFault*                       s_Active = nullptr;

        BarrierFault(Device& gpu, unsigned failAt, bool observeGroup = false) :
            device(gpu),
            original(gpu.core.CmdBufferBarrierRange),
            originalGroup(gpu.core.CmdBarrier),
            reject(failAt)
        {
            s_Active                          = this;
            device.core.CmdBufferBarrierRange = invoke;
            if (observeGroup)
            {
                device.core.CmdBarrier = invokeGroup;
            }
        }

        ~BarrierFault()
        {
            device.core.CmdBufferBarrierRange = original;
            device.core.CmdBarrier            = originalGroup;
            s_Active                          = nullptr;
        }

        static VriResult VRI_CALL invoke(VriCommandBuffer* commands, const VriBufferBarrierDesc* barrier)
        {
            auto& fault = *s_Active;
            fault.barriers.push_back(*barrier);
            if (fault.barriers.size() == fault.reject)
            {
                return VriResult_Failure;
            }
            return fault.original(commands, barrier);
        }

        static void VRI_CALL invokeGroup(VriCommandBuffer* commands, const VriBarrierGroupDesc* group)
        {
            auto& fault = *s_Active;
            for (uint32_t i = 0; i < group->bufferNum; ++i)
            {
                fault.barriers.push_back(group->buffers[i]);
            }
            fault.originalGroup(commands, group);
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

    struct RangeSupportDisabled
    {
        Device&                                   device;
        decltype(VriCoreInterface::GetDeviceDesc) original;
        VriDeviceDesc                             caps;
        inline static RangeSupportDisabled*       s_Active = nullptr;

        explicit RangeSupportDisabled(Device& gpu) :
            device(gpu),
            original(gpu.core.GetDeviceDesc),
            caps(*original(gpu.handle))
        {
            caps.hasBufferRangeBarriers = VRI_FALSE;
            s_Active                    = this;
            device.core.GetDeviceDesc   = query;
        }

        ~RangeSupportDisabled()
        {
            device.core.GetDeviceDesc = original;
            s_Active                  = nullptr;
        }

        static const VriDeviceDesc* VRI_CALL query(const VriDevice* gpu)
        {
            return gpu == s_Active->device.handle ? &s_Active->caps : s_Active->original(gpu);
        }
    };

    inline bool same(VriAccessStage a, VriAccessStage b)
    {
        return a.access == b.access && a.stages == b.stages;
    }

    inline std::expected<void, std::string> nativeStates(Device& device)
    {
        const VriAccessStage source {VriAccess_CopySourceRead, VriPipelineStage_Transfer};
        const VriAccessStage destination {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer};
        auto                 buffer = Buffer::create(device, description(1024));
        auto                 other  = Buffer::create(device, description(1024));
        auto                 frame  = Frame::create(device);
        if (!buffer || !other || !frame)
        {
            return std::unexpected("Create range state resources failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands || !(*buffer)->transitionExpected(*commands, destination) ||
            !(*buffer)->transitionExpected(*commands, source, 0, 256) || !(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Establish mixed buffer states failed");
        }
        const auto before = (*buffer)->captureState();
        const auto head   = (*buffer)->stateExpected(0, 256);
        const auto tail   = (*buffer)->stateExpected(256, 0);
        if (!head || !tail || !same(*head, source) || !same(*tail, destination) || (*buffer)->stateExpected() ||
            (*buffer)->stateExpected(1024) || (*buffer)->stateExpected(1, UINT64_MAX) ||
            (*other)->restoreUnsubmittedExpected(before))
        {
            return std::unexpected("Mixed range state query, bounds or snapshot identity was accepted incorrectly");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        VriBufferBarrierDesc invalid {(*buffer)->handle, source, destination, 1024, 0};
        if (device.core.CmdBufferBarrierRange(nullptr, &invalid) != VriResult_InvalidArgument ||
            device.core.CmdBufferBarrierRange(*commands, nullptr) != VriResult_InvalidArgument ||
            device.core.CmdBufferBarrierRange(*commands, &invalid) != VriResult_InvalidArgument)
        {
            return std::unexpected("Native byte barrier accepted a missing object or empty range");
        }
        invalid.offset = 1;
        invalid.size   = UINT64_MAX;
        if (device.core.CmdBufferBarrierRange(*commands, &invalid) != VriResult_InvalidArgument)
        {
            return std::unexpected("Native byte barrier accepted an overflowing range");
        }
        auto attachment = Texture::create(device, colorTexture({2, 2}));
        if (!attachment)
        {
            return std::unexpected(attachment.error());
        }
        auto view = (*attachment)->viewExpected();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        invalid = {(*buffer)->handle, source, destination, 0, 256};
        VriTextureViewDesc selectedView {};
        selectedView.viewType = VriTextureViewType_MaxEnum;
        if (!(*attachment)
                 ->transitionExpected(*commands,
                                      {VriAccess_ColorAttachmentWrite,
                                       VriLayout_ColorAttachment,
                                       VriPipelineStage_ColorAttachmentOutput},
                                      selectedView))
        {
            return std::unexpected("Prepare render-scope rejection attachment failed");
        }
        const std::array clear {0.f, 0.f, 0.f, 1.f};
        beginColorPass(device, *commands, *view, {2, 2}, clear.data());
        const auto insideRender = device.core.CmdBufferBarrierRange(*commands, &invalid);
        device.core.CmdEndRendering(*commands);
        if (insideRender != VriResult_InvalidArgument)
        {
            return std::unexpected("Native byte barrier accepted a render-pass scope");
        }
        auto foreignDevice = Device::create(false, nullptr, 0);
        if (!foreignDevice)
        {
            return std::unexpected(foreignDevice.error());
        }
        auto foreign = Buffer::create(**foreignDevice, description(64));
        if (!foreign)
        {
            return std::unexpected(foreign.error());
        }
        invalid = {(*foreign)->handle, source, destination, 0, 0};
        if (device.core.CmdBufferBarrierRange(*commands, &invalid) != VriResult_InvalidArgument)
        {
            return std::unexpected("Native byte barrier accepted another device's buffer");
        }
        bool rejected = false;
        {
            BarrierFault fault(device, 2);
            rejected = !(*buffer)->transitionExpected(*commands, source, 128, 384) && fault.barriers.size() == 2 &&
                       fault.barriers[0].offset == 128 && fault.barriers[0].size == 128 &&
                       fault.barriers[1].offset == 256 && fault.barriers[1].size == 256 &&
                       same(fault.barriers[0].before, source) && same(fault.barriers[1].before, destination);
        }
        if (!rejected || !((*buffer)->captureState() == before) || !(*frame)->discardExpected())
        {
            return std::unexpected("Late native barrier failure committed mixed CPU states or prevented discard");
        }
        invalid = {(*buffer)->handle, source, destination, 0, 256};
        if (device.core.CmdBufferBarrierRange(*commands, &invalid) != VriResult_InvalidArgument)
        {
            return std::unexpected("Native byte barrier accepted commands outside recording");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        {
            BarrierFault fault(device, 0);
            if (!(*buffer)->transitionExpected(*commands, destination, 64, 64, true) || fault.barriers.size() != 1 ||
                fault.barriers[0].offset != 64 || fault.barriers[0].size != 64 ||
                !same(fault.barriers[0].before, {VriAccess_None, VriPipelineStage_AllCommands}))
            {
                return std::unexpected("External dependency did not preserve the selected byte interval");
            }
        }
        const auto unchanged = (*buffer)->stateExpected(128, 128);
        if (!unchanged || !same(*unchanged, source) || !(*frame)->discardExpected() ||
            !(*buffer)->restoreUnsubmittedExpected(before) || !((*buffer)->captureState() == before))
        {
            return std::unexpected("Selected transition modified adjacent bytes or snapshot restoration failed");
        }
        commands = (*frame)->beginExpected();
        if (!commands || !(*buffer)->transitionExpected(*commands, source, 256, 0) ||
            !(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Retry mixed range transition failed");
        }
        auto uniform = (*buffer)->stateExpected();
        if (!uniform || !same(*uniform, source))
        {
            return std::unexpected("Equal adjacent byte states did not coalesce");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        {
            RangeSupportDisabled disabled(device);
            if ((*buffer)->transitionExpected(*commands, destination, 64, 64) ||
                !(*buffer)->transitionExpected(*commands, destination))
            {
                return std::unexpected("Disabled range capability did not reject partial and preserve whole barriers");
            }
            RenderGraph graph(device);
            auto        resource = graph.createBufferExpected("unsupported", description(1024));
            if (!resource ||
                !graph.addPassExpected(
                    "unsupported",
                    std::array {use(*resource, Usage::eCopyDestination, 0, 128, true)},
                    [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, std::string>
                    {
                        return {};
                    },
                    true) ||
                graph.compileExpected() || graph.getBufferExpected(*resource))
            {
                return std::unexpected("Disabled range capability reached graph allocation");
            }
        }
        if (!(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Whole-buffer barrier stopped working with partial range support disabled");
        }
        std::puts(
            "BUFFER_RANGE_STATES native_bounds=checked foreign_device=rejected late_failure=restored retry=coalesced");
        return {};
    }

    inline std::expected<void, std::string> plannerRejections(Device& device)
    {
        auto imported = Buffer::create(device, description(1024));
        if (!imported)
        {
            return std::unexpected(imported.error());
        }
        const auto noop = [](VriCommandBuffer*, RenderGraph&) -> std::expected<void, std::string>
        {
            return {};
        };
        for (unsigned test = 0; test < 6; ++test)
        {
            RenderGraph graph(device);
            auto        arena = graph.createBufferExpected("arena", description(1024));
            if (!arena)
            {
                return std::unexpected(arena.error());
            }
            auto selected = use(*arena, Usage::eCopyDestination, 0, 128, true);
            if (test == 0)
            {
                selected.bufferRange = {1024, 0};
            }
            else if (test == 1)
            {
                selected.bufferRange = {1, UINT64_MAX};
            }
            else if (test == 2)
            {
                selected.range.baseMip = 1;
            }
            if (!graph.addPassExpected("write", std::array {selected}, noop, true))
            {
                return std::unexpected("Add negative range writer failed");
            }
            if (test == 3 &&
                !graph.addPassExpected("hole", std::array {use(*arena, Usage::eCopySource, 0, 256)}, noop, true))
            {
                return std::unexpected("Add hole read failed");
            }
            if (test == 4 && !graph.exportResourceExpected(*arena))
            {
                return std::unexpected("Export negative hole buffer failed");
            }
            if (test == 5)
            {
                auto image = graph.createTextureExpected("image", colorTexture({2, 2}));
                if (!image || !graph.addPassExpected("wrong-kind",
                                                     std::array {use(*image, Usage::eColorWrite, 1, 1, true)},
                                                     noop,
                                                     true))
                {
                    return std::unexpected("Add wrong-kind range failed");
                }
            }
            auto compiled = graph.compileExpected();
            if (compiled || graph.getBufferExpected(*arena))
            {
                return std::unexpected("Invalid byte range/hole reached resource allocation: " + std::to_string(test));
            }
        }
        RenderGraph overlap(device);
        auto        arena = overlap.importResourceExpected("arena", **imported);
        if (!arena ||
            !overlap.addPassExpected(
                "overlap",
                std::array {use(*arena, Usage::eCopySource, 0, 512), use(*arena, Usage::eCopyDestination, 256, 512)},
                noop,
                true) ||
            overlap.compileExpected())
        {
            return std::unexpected("Overlapping read/write declarations were not rejected");
        }
        RenderGraph discard(device);
        auto        temporary = discard.createBufferExpected("discard", description(1024));
        if (!temporary ||
            !discard.addPassExpected("obsolete",
                                     std::array {use(*temporary, Usage::eCopyDestination, 0, 256, true)},
                                     noop) ||
            !discard.addPassExpected("replacement",
                                     std::array {use(*temporary, Usage::eCopyDestination, 0, 256, true)},
                                     noop) ||
            !discard.addPassExpected("read", std::array {use(*temporary, Usage::eCopySource, 0, 256)}, noop, true) ||
            !discard.compileExpected())
        {
            return std::unexpected("Compile selected discard culling graph failed");
        }
        const auto snapshot = discard.snapshot();
        if (snapshot.passes[0].active || !snapshot.passes[1].active || !snapshot.passes[2].active ||
            !snapshot.passes[1].contentDependencies.empty())
        {
            return std::unexpected("Selected discard retained an overwritten producer");
        }
        std::puts("BUFFER_RANGE_PLANNER invalid=7 holes=rejected overlap=rejected selected_discard=culled");
        return {};
    }

    inline std::expected<void, std::string> workload(Device& device, Inspect inspect)
    {
        constexpr uint64_t inputBytes  = 2048;
        constexpr uint64_t outputBytes = 1536;
        auto               input       = Buffer::create(device, description(inputBytes, VriMemoryLocation_HostUpload));
        if (!input)
        {
            return std::unexpected(input.error());
        }
        RenderGraph graph(device);
        auto        source = graph.importResourceExpected("input", **input);
        auto        arena  = graph.createBufferExpected("arena", description(inputBytes));
        auto output = graph.createBufferExpected("output", description(outputBytes, VriMemoryLocation_HostReadback));
        if (!source || !arena || !output)
        {
            return std::unexpected("Create range graph resources failed");
        }
        const auto addCopy = [&](const char* name, uint64_t from, uint64_t to, uint64_t length)
        {
            const std::array uses {use(*source, Usage::eCopySource, from, length),
                                   use(*arena, Usage::eCopyDestination, to, length, true)};
            return graph.addPassExpected(name,
                                         uses,
                                         [&, from, to, length](auto* commands, auto& current)
                                         {
                                             return copy(device, commands, current, *source, *arena, from, to, length);
                                         });
        };
        if (!addCopy("unused", 1536, 1536, 512) || !addCopy("left", 0, 0, 512) || !addCopy("right", 512, 512, 512) ||
            !addCopy("replace", 1024, 0, 256))
        {
            return std::unexpected("Add independent range writers failed");
        }
        enum class CallbackFault
        {
            eNone,
            eWiderRange,
            eLegacyRange,
            eWholeState,
            eWritableInput,
            eOtherCommands,
            eWiderRestore,
            eDiscardedRestore,
            eUndeclaredBuffer
        };
        CallbackFault callbackFault = CallbackFault::eNone;
        VriCommandBuffer* wrongCommands = nullptr;
        bool noCallbackBarriers = true;
        constexpr VriAccessStage destinationWrite {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer};
        const std::array tailUses {use(*source, Usage::eCopySource, 1024, 512),
                                   use(*arena, Usage::eCopyDestination, 1280, 256, true),
                                   use(*arena, Usage::eCopyDestination, 1024, 256, true)};
        const std::array gatherUses {use(*arena, Usage::eCopySource, 0, outputBytes),
                                     use(*output, Usage::eCopyDestination, 0, 0, true)};
        if (!graph.addPassExpected("tail",
                                   tailUses,
                                   [&](auto* commands, auto& current) -> std::expected<void, std::string>
                                   {
                                       auto buffer = current.getBufferExpected(*arena);
                                       if (!buffer)
                                       {
                                           return std::unexpected(buffer.error());
                                       }
                                       if (callbackFault != CallbackFault::eNone)
                                       {
                                           BarrierFault trace(device, 0, true);
                                           const auto attempt = [&]() -> std::expected<void, std::string>
                                           {
                                               switch (callbackFault)
                                               {
                                                   case CallbackFault::eWiderRange:
                                                       return (*buffer)->transitionExpected(commands, destinationWrite,
                                                                                           1024, 0);
                                                   case CallbackFault::eLegacyRange:
                                                       (*buffer)->transition(commands, destinationWrite, 1024, 768);
                                                       return {};
                                                   case CallbackFault::eWholeState:
                                                       (*buffer)->setState(destinationWrite);
                                                       return {};
                                                   case CallbackFault::eWritableInput:
                                                       return (*input)->transitionExpected(commands, destinationWrite,
                                                                                          1024, 512);
                                                   case CallbackFault::eOtherCommands:
                                                       return (*buffer)->transitionExpected(wrongCommands, destinationWrite,
                                                                                           1024, 512);
                                                   case CallbackFault::eWiderRestore:
                                                       return (*buffer)->restoreExpected(commands, (*buffer)->captureState(),
                                                                                         1024, 768);
                                                   case CallbackFault::eDiscardedRestore:
                                                       return (*buffer)->restoreUnsubmittedExpected((*buffer)->captureState());
                                                   case CallbackFault::eUndeclaredBuffer:
                                                   {
                                                       auto unrelated = current.getBufferExpected(*output);
                                                       if (!unrelated)
                                                       {
                                                           return std::unexpected(unrelated.error());
                                                       }
                                                       return (*unrelated)->transitionExpected(commands, destinationWrite);
                                                   }
                                                   case CallbackFault::eNone:
                                                       return {};
                                               }
                                               return std::unexpected("Unknown callback fault");
                                           }();
                                           noCallbackBarriers = trace.barriers.empty();
                                           return attempt;
                                       }
                                       if (auto merged = (*buffer)->transitionExpected(commands, destinationWrite, 1024, 512);
                                           !merged)
                                       {
                                           return merged;
                                       }
                                       return copy(device, commands, current, *source, *arena, 1024, 1024, 512);
                                   }) ||
            !graph.addPassExpected("gather",
                                   gatherUses,
                                   [&](auto* commands, auto& current)
                                   {
                                       return copy(device, commands, current, *arena, *output, 0, 0, outputBytes);
                                   }) ||
            !graph.exportResourceExpected(*output))
        {
            return std::unexpected("Add range gather/export failed");
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        const auto plan = graph.snapshot();
        if (plan.passes[0].active || !plan.passes[1].dependencies.empty() || !plan.passes[2].dependencies.empty() ||
            !plan.passes[4].dependencies.empty() || !plan.passes[3].contentDependencies.empty() ||
            plan.passes[5].contentDependencies != std::vector<size_t> {1, 2, 3, 4})
        {
            return std::unexpected("Byte-range graph retained false dependencies or lost a live producer");
        }
        auto graphics = Frame::create(device);
        auto compute  = Frame::create(device, VriQueueType_Compute);
        auto transfer = Frame::create(device, VriQueueType_Transfer);
        if (!graphics || !compute || !transfer)
        {
            return std::unexpected("Create range workload frames failed");
        }
        std::array<Fence, 3> fences {Fence(nullptr, device.core.DestroyFence),
                                     Fence(nullptr, device.core.DestroyFence),
                                     Fence(nullptr, device.core.DestroyFence)};
        for (auto& fence : fences)
        {
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            fence.reset(raw);
            if (status != VriResult_Success || !fence)
            {
                return std::unexpected("Create range workload fence failed");
            }
        }
        const auto recordRouted = [&](VriCommandBuffer* graphicsCommands,
                                       VriCommandBuffer* computeCommands,
                                       VriCommandBuffer* transferCommands)
        {
            const std::array destinations {RenderGraph::PassRecording {"left", graphicsCommands},
                                           RenderGraph::PassRecording {"right", computeCommands},
                                           RenderGraph::PassRecording {"replace", graphicsCommands},
                                           RenderGraph::PassRecording {"tail", computeCommands},
                                           RenderGraph::PassRecording {"gather", transferCommands}};
            const std::array order {RenderGraph::RecordingDependency {graphicsCommands, transferCommands},
                                    RenderGraph::RecordingDependency {computeCommands, transferCommands}};
            return graph.executeOrderedExpected(graphicsCommands, destinations, order);
        };
        const auto resultBuffer = graph.getBufferExpected(*output);
        const auto arenaBuffer  = graph.getBufferExpected(*arena);
        if (!resultBuffer || !arenaBuffer)
        {
            return std::unexpected("Range graph buffers disappeared");
        }
        const auto verify = [&](unsigned cycle) -> std::expected<void, std::string>
        {
            const auto* mapped =
                static_cast<const uint32_t*>(device.core.MapBuffer((*resultBuffer)->handle, 0, outputBytes));
            if (!mapped)
            {
                return std::unexpected("Map range readback failed");
            }
            bool matches = true;
            for (uint32_t i = 0; i < outputBytes / sizeof(uint32_t); ++i)
            {
                const auto selected = i < 64 ? i + 256 : i;
                matches &= mapped[i] == 0x10200000u + cycle * 4096 + selected;
            }
            device.core.UnmapBuffer((*resultBuffer)->handle);
            if (!matches)
            {
                return std::unexpected("Byte-range workload GPU content mismatch");
            }
            return {};
        };
        for (unsigned cycle = 0; cycle < 6; ++cycle)
        {
            auto* mapped = static_cast<uint32_t*>(device.core.MapBuffer((*input)->handle, 0, inputBytes));
            if (!mapped)
            {
                return std::unexpected("Map range workload input failed");
            }
            for (uint32_t i = 0; i < inputBytes / sizeof(uint32_t); ++i)
            {
                mapped[i] = 0x10200000u + cycle * 4096 + i;
            }
            device.core.UnmapBuffer((*input)->handle);
            if (cycle == 1)
            {
                using enum CallbackFault;
                const std::array cases {std::pair {eWiderRange, "outside its declared ranges"},
                                        std::pair {eLegacyRange, "outside its declared ranges"},
                                        std::pair {eWholeState, "outside its declared ranges"},
                                        std::pair {eWritableInput, "declared read-only range"},
                                        std::pair {eOtherCommands, "another recording destination"},
                                        std::pair {eWiderRestore, "outside its declared ranges"},
                                        std::pair {eDiscardedRestore, "cannot restore a discarded recording"},
                                        std::pair {eUndeclaredBuffer, "output: buffer callback accessed bytes outside its declared ranges"}};
                const auto beforeArena = (*arenaBuffer)->captureState();
                const auto beforeInput = (*input)->captureState();
                const auto beforeOutput = (*resultBuffer)->captureState();
                for (const auto& [kind, reason] : cases)
                {
                    auto a = (*graphics)->beginExpected();
                    auto b = (*compute)->beginExpected();
                    auto c = (*transfer)->beginExpected();
                    if (!a || !b || !c)
                    {
                        return std::unexpected("Begin routed callback guard probe failed");
                    }
                    callbackFault = kind;
                    wrongCommands = *a;
                    auto rejected = recordRouted(*a, *b, *c);
                    callbackFault = eNone;
                    wrongCommands = nullptr;
                    if (rejected || rejected.error().find(reason) == std::string::npos || !noCallbackBarriers ||
                        !((*arenaBuffer)->captureState() == beforeArena) ||
                        !((*input)->captureState() == beforeInput) || !((*resultBuffer)->captureState() == beforeOutput) ||
                        !(*graphics)->discardExpected() || !(*compute)->discardExpected() || !(*transfer)->discardExpected())
                    {
                        return std::unexpected("Routed callback guard did not reject and restore: " +
                                               std::to_string(unsigned(kind)));
                    }
                }
                if (auto unchanged = verify(0); !unchanged)
                {
                    return unchanged;
                }
                std::puts("BUFFER_RANGE_CALLBACK_GUARD rejected=8 unauthorized_native_barriers=0 checkpoint=restored");
            }
            if (cycle == 2)
            {
                const auto beforeArena    = (*arenaBuffer)->captureState();
                const auto beforeInput    = (*input)->captureState();
                const auto beforeOutput   = (*resultBuffer)->captureState();
                auto       failedCommands = (*graphics)->beginExpected();
                if (!failedCommands)
                {
                    return std::unexpected(failedCommands.error());
                }
                bool rejected = false;
                {
                    BarrierFault fault(device, 6);
                    rejected = !graph.executeExpected(*failedCommands) && fault.barriers.size() == 6;
                }
                if (!rejected || !((*arenaBuffer)->captureState() == beforeArena) ||
                    !((*input)->captureState() == beforeInput) || !((*resultBuffer)->captureState() == beforeOutput) ||
                    !(*graphics)->discardExpected())
                {
                    return std::unexpected("Late graph range failure did not restore all interval states");
                }
                if (auto unchanged = verify(cycle - 1); !unchanged)
                {
                    return unchanged;
                }
            }
            if (cycle == 4)
            {
                for (const bool rejectEnd : {true, false})
                {
                    auto       checkpoint     = graph.checkpointExpected();
                    const auto before         = (*arenaBuffer)->captureState();
                    auto       failedCommands = (*graphics)->beginExpected();
                    if (!checkpoint || !failedCommands)
                    {
                        return std::unexpected("Capture selected-range recording checkpoint failed");
                    }
                    if (auto recorded = graph.executeExpected(*failedCommands); !recorded)
                    {
                        return recorded;
                    }
                    if (!(*arenaBuffer)
                             ->transitionExpected(*failedCommands,
                                                  {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer},
                                                  128,
                                                  128) ||
                        (*arenaBuffer)->captureState() == before)
                    {
                        return std::unexpected("Selected checkpoint fault did not change interval tracking");
                    }
                    bool rejected = false;
                    {
                        RecordingFault fault(device, rejectEnd);
                        rejected = !(*graphics)->submitExpected() && fault.calls == 1;
                    }
                    if (!rejected || !(*graphics)->discardExpected() ||
                        !graph.restoreUnsubmittedExpected(std::move(*checkpoint)) ||
                        !((*arenaBuffer)->captureState() == before))
                    {
                        return std::unexpected(
                            "Known-rejected submission did not restore the full interval checkpoint");
                    }
                    if (auto unchanged = verify(cycle - 1); !unchanged)
                    {
                        return unchanged;
                    }
                }
                std::puts(
                    "BUFFER_RANGE_CHECKPOINT end_reject=1 submit_reject=1 intervals=restored old_pixels=unchanged");
            }
            auto commands = (*graphics)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            std::vector<VriFenceSubmitDesc> previous;
            if (cycle)
            {
                previous.push_back({fences[2].get(), cycle, VriPipelineStage_AllCommands});
            }
            const std::array complete {VriFenceSubmitDesc {fences[2].get(), cycle + 1, VriPipelineStage_AllCommands}};
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
                auto transferCommands = (*transfer)->beginExpected();
                if (!computeCommands || !transferCommands)
                {
                    return std::unexpected("Begin routed range workload failed");
                }
                if (auto recorded = recordRouted(*commands, *computeCommands, *transferCommands); !recorded)
                {
                    return recorded;
                }
                const std::array leftReady {
                    VriFenceSubmitDesc {fences[0].get(), cycle + 1, VriPipelineStage_AllCommands}};
                const std::array rightReady {
                    VriFenceSubmitDesc {fences[1].get(), cycle + 1, VriPipelineStage_AllCommands}};
                const std::array produced {leftReady[0], rightReady[0]};
                if (auto submitted = (*graphics)->submitExpected(previous, leftReady); !submitted)
                {
                    return submitted;
                }
                if (auto submitted = (*compute)->submitExpected(previous, rightReady); !submitted)
                {
                    return submitted;
                }
                if (auto submitted = (*transfer)->submitExpected(produced, complete); !submitted)
                {
                    return submitted;
                }
                if (!(*transfer)->waitExpected() || !(*compute)->waitExpected())
                {
                    return std::unexpected("Wait routed range workload failed");
                }
            }
            if (auto waited = (*graphics)->waitExpected(); !waited)
            {
                return waited;
            }
            if (auto checked = verify(cycle); !checked)
            {
                return checked;
            }
            const auto untouched = (*arenaBuffer)->stateExpected(1536, 512);
            if (!untouched || untouched->access != VriAccess_None || (*arenaBuffer)->stateExpected())
            {
                return std::unexpected("Routed recording overwrote the unused interval's access state");
            }
        }
        if (inspect)
        {
            if (auto checked = inspect(graph); !checked)
            {
                return checked;
            }
        }
        std::puts(
            "BUFFER_RANGE_GPU frames=6 exact_words=2304 disjoint_producers=2 destinations=3 late_failure=restored");
        return {};
    }

    inline std::expected<void, std::string> heaps(Device& device)
    {
        auto input = Buffer::create(device, description(512, VriMemoryLocation_HostUpload));
        if (!input)
        {
            return std::unexpected(input.error());
        }
        auto* mapped = static_cast<uint32_t*>(device.core.MapBuffer((*input)->handle, 0, 512));
        if (!mapped)
        {
            return std::unexpected("Map range heap input failed");
        }
        for (uint32_t i = 0; i < 128; ++i)
        {
            mapped[i] = 0x77880000u + i;
        }
        device.core.UnmapBuffer((*input)->handle);
        for (const bool discard : {true, false})
        {
            RenderGraph graph(device);
            auto        source = graph.importResourceExpected("input", **input);
            auto        a      = graph.createBufferExpected("a", description(1024));
            auto        b      = graph.createBufferExpected("b", description(1024));
            auto        output = graph.createBufferExpected("output", description(512, VriMemoryLocation_HostReadback));
            if (!source || !a || !b || !output)
            {
                return std::unexpected("Create selected-range heap graph failed");
            }
            const auto add = [&](const char* name, auto from, auto to, uint64_t offset, bool firstDiscard)
            {
                const std::array uses {use(from, Usage::eCopySource, offset, 256),
                                       use(to, Usage::eCopyDestination, offset, 256, firstDiscard)};
                return graph.addPassExpected(name,
                                             uses,
                                             [&, from, to, offset](auto* commands, auto& current)
                                             {
                                                 return copy(device, commands, current, from, to, offset, offset, 256);
                                             });
            };
            if (!add("a-write", *source, *a, 0, true) || !add("a-read", *a, *output, 0, true) ||
                !add("b-write", *source, *b, 256, discard) || !add("b-read", *b, *output, 256, true) ||
                !graph.addOrderingExpected("a-read", "b-write") || !graph.exportResourceExpected(*output))
            {
                return std::unexpected("Add selected-range heap passes failed");
            }
            if (auto compiled = graph.compileHeapsExpected(); !compiled)
            {
                return compiled;
            }
            const auto  plan  = graph.snapshot();
            const auto& aInfo = plan.resources[a->index];
            const auto& bInfo = plan.resources[b->index];
            if ((discard &&
                 (aInfo.heap == UINT32_MAX || aInfo.heap != bInfo.heap || aInfo.allocation == bInfo.allocation)) ||
                (!discard && bInfo.heap != UINT32_MAX))
            {
                return std::unexpected("Heap eligibility ignored selected first use or treated unused bytes as live");
            }
            auto frame = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto result = graph.getBufferExpected(*output);
            if (!result)
            {
                return std::unexpected(result.error());
            }
            const auto* values = static_cast<const uint32_t*>(device.core.MapBuffer((*result)->handle, 0, 512));
            if (!values)
            {
                return std::unexpected("Map range heap output failed");
            }
            bool matches = true;
            for (uint32_t i = 0; i < 128; ++i)
            {
                matches &= values[i] == 0x77880000u + i;
            }
            device.core.UnmapBuffer((*result)->handle);
            if (!matches)
            {
                return std::unexpected("Selected-range native heap GPU content mismatch");
            }
        }
        std::puts("BUFFER_RANGE_HEAPS exact_words=256 unused_bytes=excluded preserving_first_use=excluded");
        return {};
    }

    inline std::expected<void, std::string> probe(Device& device, Inspect inspect = nullptr)
    {
        const auto* capabilities = device.core.GetDeviceDesc(device.handle);
        if (!capabilities || !capabilities->hasBufferRangeBarriers || !device.core.CmdBufferBarrierRange)
        {
            return std::unexpected("Byte-range probe requires native buffer range support");
        }
        if (auto result = plannerRejections(device); !result)
        {
            return result;
        }
        if (auto result = nativeStates(device); !result)
        {
            return result;
        }
        if (auto result = workload(device, inspect); !result)
        {
            return result;
        }
        return heaps(device);
    }
} // namespace vultra::test::buffer_ranges_probe
