#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace vultra::test::resource_retirement_probe
{
    struct DestroyFence
    {
        Device* device;

        void operator()(VriFence* fence) const noexcept
        {
            device->core.DestroyFence(fence);
        }
    };

    using Fence = std::unique_ptr<VriFence, DestroyFence>;

    struct NativeFault
    {
        Device&                         device;
        VriCoreInterface                original;
        std::set<VriBuffer*>            buffers;
        std::set<VriMemory*>            memories;
        std::set<VriFence*>             fences;
        std::set<VriPipeline*>          pipelines;
        std::vector<VriFenceSubmitDesc> lastSignals;
        VriFence*                       heldFence        = nullptr;
        VriResult                       pollResult       = VriResult_Success;
        bool                            failCreate       = false;
        bool                            failEnd          = false;
        bool                            failSubmit       = false;
        unsigned                        idleCalls        = 0;
        unsigned                        createFenceCalls = 0;
        unsigned                        beginCalls       = 0;
        inline static NativeFault*      s_Active         = nullptr;

        explicit NativeFault(Device& value) :
            device(value),
            original(value.core)
        {
            s_Active                       = this;
            device.core.CreateBuffer       = createBuffer;
            device.core.DestroyBuffer      = destroyBuffer;
            device.core.AllocateMemory     = allocate;
            device.core.FreeMemory         = free;
            device.core.CreateFence        = createFence;
            device.core.DestroyFence       = destroyFence;
            device.core.WaitForFence       = wait;
            device.core.DeviceWaitIdle     = idle;
            device.core.BeginCommandBuffer = begin;
            device.core.EndCommandBuffer   = end;
            device.core.QueueSubmit        = submit;
            device.core.DestroyPipeline    = destroyPipeline;
        }

        ~NativeFault()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL createBuffer(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
        {
            auto&      self   = *s_Active;
            const auto status = self.original.CreateBuffer(device, desc, output);
            if (status == VriResult_Success && *output)
            {
                self.buffers.insert(*output);
            }
            return status;
        }

        static void VRI_CALL destroyBuffer(VriBuffer* value)
        {
            s_Active->buffers.erase(value);
            s_Active->original.DestroyBuffer(value);
        }

        static VriResult VRI_CALL allocate(VriDevice* device, const VriMemoryDesc* desc, VriMemory** output)
        {
            auto&      self   = *s_Active;
            const auto status = self.original.AllocateMemory(device, desc, output);
            if (status == VriResult_Success && *output)
            {
                self.memories.insert(*output);
            }
            return status;
        }

        static void VRI_CALL free(VriMemory* value)
        {
            s_Active->memories.erase(value);
            s_Active->original.FreeMemory(value);
        }

        static VriResult VRI_CALL createFence(VriDevice* device, uint64_t initial, VriFence** output)
        {
            auto& self = *s_Active;
            ++self.createFenceCalls;
            if (self.failCreate)
            {
                *output = nullptr;
                return VriResult_OutOfMemory;
            }
            const auto status = self.original.CreateFence(device, initial, output);
            if (status == VriResult_Success && *output)
            {
                self.fences.insert(*output);
            }
            return status;
        }

        static void VRI_CALL destroyFence(VriFence* value)
        {
            s_Active->fences.erase(value);
            s_Active->original.DestroyFence(value);
        }

        static VriResult VRI_CALL wait(VriFence* fence, uint64_t value, uint64_t timeout)
        {
            auto& self = *s_Active;
            if (!timeout && fence == self.heldFence && self.pollResult != VriResult_Success)
            {
                return self.pollResult;
            }
            return self.original.WaitForFence(fence, value, timeout);
        }

        static VriResult VRI_CALL idle(VriDevice* device)
        {
            ++s_Active->idleCalls;
            return s_Active->original.DeviceWaitIdle(device);
        }

        static VriResult VRI_CALL begin(VriCommandBuffer* commands)
        {
            ++s_Active->beginCalls;
            return s_Active->original.BeginCommandBuffer(commands);
        }

        static VriResult VRI_CALL end(VriCommandBuffer* commands)
        {
            return s_Active->failEnd ? VriResult_Failure : s_Active->original.EndCommandBuffer(commands);
        }

        static VriResult VRI_CALL submit(VriQueue* queue, const VriQueueSubmitDesc* desc)
        {
            auto& self = *s_Active;
            self.lastSignals.assign(desc->signalFences, desc->signalFences + desc->signalFenceNum);
            return self.failSubmit ? VriResult_Failure : self.original.QueueSubmit(queue, desc);
        }

        static void VRI_CALL destroyPipeline(VriPipeline* pipeline)
        {
            s_Active->pipelines.erase(pipeline);
            s_Active->original.DestroyPipeline(pipeline);
        }
    };

    struct Drain
    {
        Device& device;

        ~Drain()
        {
            device.waitIdleForDestruction();
            if (auto collected = device.collectDeferredExpected(); !collected)
            {
                std::fprintf(stderr, "Drain retirement fixture: %s\n", collected.error().c_str());
            }
        }
    };

    struct Counted
    {
        unsigned& destroyed;

        ~Counted()
        {
            ++destroyed;
        }
    };

    inline VriBufferDesc bufferDesc(VriMemoryLocation location = VriMemoryLocation_Device)
    {
        return {1024,
                0,
                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                location};
    }

    struct Bundle
    {
        size_t                                 recordedCopies = 0;
        unsigned&                              destroyed;
        std::array<std::unique_ptr<Buffer>, 2> inputs;
        std::array<Fence, 3>                   ready;
        std::unique_ptr<RenderGraph>           graph;
        std::array<RenderGraph::Resource, 3>   outputs {};

        Bundle(Device& device, unsigned& counter) :
            destroyed(counter),
            ready {Fence(nullptr, DestroyFence {&device}),
                   Fence(nullptr, DestroyFence {&device}),
                   Fence(nullptr, DestroyFence {&device})},
            graph(std::make_unique<RenderGraph>(device))
        {
        }

        ~Bundle()
        {
            ++destroyed;
        }

        std::expected<void, std::string> initialize(Device& device, uint32_t seed, bool orderInGraph)
        {
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                auto created = Buffer::create(device, bufferDesc(VriMemoryLocation_HostUpload));
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                inputs[i]    = std::move(*created);
                auto* values = static_cast<uint32_t*>(device.core.MapBuffer(inputs[i]->handle, 0, 1024));
                if (!values)
                {
                    return std::unexpected("Map retirement graph input failed");
                }
                for (uint32_t word = 0; word < 256; ++word)
                {
                    values[word] = seed + uint32_t(i) * 65536 + word;
                }
                device.core.UnmapBuffer(inputs[i]->handle);
            }
            for (auto& fence : ready)
            {
                VriFence*  raw    = nullptr;
                const auto status = device.core.CreateFence(device.handle, 0, &raw);
                fence.reset(raw);
                if (status != VriResult_Success || !fence)
                {
                    return std::unexpected("Create retirement graph routing fence failed");
                }
            }
            auto a    = graph->importResourceExpected("input-a", *inputs[0]);
            auto b    = graph->importResourceExpected("input-b", *inputs[1]);
            auto old  = graph->createBufferExpected("old-heap-member", bufferDesc());
            auto next = graph->createBufferExpected("next-heap-member", bufferDesc());
            if (!a || !b || !old || !next)
            {
                return std::unexpected("Create retirement graph resources failed");
            }
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                auto output = graph->createBufferExpected("output-" + std::to_string(i),
                                                          bufferDesc(VriMemoryLocation_HostReadback));
                if (!output)
                {
                    return std::unexpected(output.error());
                }
                outputs[i] = *output;
            }
            const auto add = [&](const char* name, RenderGraph::Resource source, RenderGraph::Resource destination)
            {
                const std::array uses {RenderGraph::Use {source, Usage::eCopySource},
                                       RenderGraph::Use {destination, Usage::eCopyDestination, true}};
                return graph->addPassExpected(
                    name,
                    uses,
                    [this, &device, source, destination](auto* commands,
                                                         auto& current) -> std::expected<void, std::string>
                    {
                        ++recordedCopies;
                        auto from = current.getBufferExpected(source);
                        auto to   = current.getBufferExpected(destination);
                        if (!from || !to)
                        {
                            return std::unexpected("Retirement graph copy resources are unavailable");
                        }
                        VriBufferCopyDesc copy {};
                        copy.size = 1024;
                        device.core.CmdCopyBuffer(commands, (*to)->handle, (*from)->handle, &copy);
                        return {};
                    });
            };
            if (!add("upload", *a, *old) || !add("read-a", *old, outputs[0]) || !add("read-b", *old, outputs[1]) ||
                !add("replace", *b, *next) || !add("save-next", *next, outputs[2]))
            {
                return std::unexpected("Declare retirement graph passes failed");
            }
            if (orderInGraph &&
                (!graph->addOrderingExpected("read-a", "replace") || !graph->addOrderingExpected("read-b", "replace")))
            {
                return std::unexpected("Order retirement graph readers before replacement failed");
            }
            for (const auto output : outputs)
            {
                if (!graph->exportResourceExpected(output))
                {
                    return std::unexpected("Export retirement graph output failed");
                }
            }
            return {};
        }
    };

    inline std::expected<void, std::string> graphLifetime(Device& device)
    {
        unsigned    destroyed       = 0;
        unsigned    secondDestroyed = 0;
        Drain       drain {device};
        NativeFault faults(device);
        for (unsigned cycle = 0; cycle < 3; ++cycle)
        {
            const auto seed  = 0x12560000u + cycle * 1024;
            auto       owner = std::make_unique<Bundle>(device, destroyed);
            const bool orderInGraph   = cycle == 0;
            const bool orderAtCompile = cycle == 1;
            if (auto initialized = owner->initialize(device, seed, orderInGraph); !initialized)
            {
                return initialized;
            }
            auto*                                 graph = owner->graph.get();
            const std::array                      types {VriQueueType_Graphics,
                                    VriQueueType_Compute,
                                    VriQueueType_Transfer,
                                    VriQueueType_Graphics};
            std::array<std::unique_ptr<Frame>, 4> frames;
            std::array<VriCommandBuffer*, 4>      commands {};
            for (size_t i = 0; i < frames.size(); ++i)
            {
                auto created = Frame::create(device, types[i]);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                frames[i]   = std::move(*created);
                commands[i] = frames[i]->recordingDestination();
            }
            const std::array destinations {RenderGraph::PassRecording {"upload", commands[0]},
                                           RenderGraph::PassRecording {"read-a", commands[1]},
                                           RenderGraph::PassRecording {"read-b", commands[2]},
                                           RenderGraph::PassRecording {"replace", commands[3]},
                                           RenderGraph::PassRecording {"save-next", commands[3]}};
            const std::array order {RenderGraph::RecordingDependency {commands[0], commands[1]},
                                    RenderGraph::RecordingDependency {commands[0], commands[2]},
                                    RenderGraph::RecordingDependency {commands[1], commands[3]},
                                    RenderGraph::RecordingDependency {commands[2], commands[3]}};
            std::expected<void, std::string> compiled;
            if (orderAtCompile)
            {
                auto proposed        = destinations;
                proposed[1].commands = commands[3];
                const std::array proposedOrder {order[1], order[3]};
                const auto       allocated = faults.memories.size();
                auto rejected = graph->compileHeapsExpected(commands[0], proposed, std::span(proposedOrder).subspan(1));
                if (rejected || rejected.error().find("required submission dependency") == std::string::npos ||
                    faults.memories.size() != allocated || owner->recordedCopies != 0)
                {
                    return std::unexpected("Heap compilation accepted an incomplete pass schedule");
                }
                compiled = graph->compileHeapsExpected(commands[0], proposed, proposedOrder);
            }
            else
            {
                compiled = graph->compileHeapsExpected();
            }
            if (!compiled)
            {
                return compiled;
            }
            auto old  = graph->findResourceExpected("old-heap-member");
            auto next = graph->findResourceExpected("next-heap-member");
            if (!old || !next)
            {
                return std::unexpected("Retirement fixture lost its heap members");
            }
            const auto before      = graph->resourceInfo(*old);
            const auto replacement = graph->resourceInfo(*next);
            const bool overlaps    = before.heapOffset < replacement.heapOffset + replacement.heapBytes &&
                                  replacement.heapOffset < before.heapOffset + before.heapBytes;
            if (before.heap == UINT32_MAX || before.heap != replacement.heap || !before.heapBytes ||
                !replacement.heapBytes || overlaps != (orderInGraph || orderAtCompile))
            {
                return std::unexpected("Native heap placement disagrees with the proven resource lifetimes");
            }
            std::array<Buffer*, 3> outputs {};
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                auto buffer = graph->getBufferExpected(owner->outputs[i]);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                outputs[i] = *buffer;
            }
            const auto liveBuffers = faults.buffers;
            const auto liveMemory  = faults.memories;

            if (orderAtCompile)
            {
                auto valid = graph->validateOrderedRecordingExpected(commands[0], destinations, order);
                if (!valid)
                {
                    return std::unexpected(valid.error());
                }
                if (owner->recordedCopies != 0)
                {
                    return std::unexpected("Heap schedule preflight called a pass");
                }
            }
            for (size_t i = 0; i < frames.size(); ++i)
            {
                auto begun = frames[i]->beginExpected();
                if (!begun || *begun != commands[i])
                {
                    return std::unexpected(begun ? "Frame changed its planned recording destination" : begun.error());
                }
            }

            if (orderInGraph || orderAtCompile)
            {
                auto inferred = graph->executeExpected(commands[0], destinations);
                if (inferred || inferred.error().find("old-heap-member -> next-heap-member") == std::string::npos ||
                    owner->recordedCopies != 0)
                {
                    return std::unexpected("Cross-destination buffer alias accepted inferred order or called a pass");
                }
                const auto reason =
                    orderAtCompile ? "does not prove native heap handoff" : "required submission dependency";
                for (size_t omitted = 2; omitted < order.size(); ++omitted)
                {
                    auto incomplete = order;
                    std::swap(incomplete[omitted], incomplete.back());
                    auto rejected = graph->executeOrderedExpected(commands[0],
                                                                  destinations,
                                                                  std::span(incomplete).first(incomplete.size() - 1));
                    if (rejected || rejected.error().find(reason) == std::string::npos || owner->recordedCopies != 0)
                    {
                        return std::unexpected("A missing alias reader edge recorded commands");
                    }
                }
            }
            if (auto recorded = graph->executeOrderedExpected(commands[0], destinations, order); !recorded)
            {
                return recorded;
            }
            if (owner->recordedCopies != destinations.size())
            {
                return std::unexpected("Ordered heap handoff lost a copy pass");
            }
            std::vector<SubmissionCompletion> completions;
            const std::array signals {VriFenceSubmitDesc {owner->ready[0].get(), 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {owner->ready[1].get(), 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {owner->ready[2].get(), 1, VriPipelineStage_AllCommands}};
            for (size_t i = 0; i < frames.size(); ++i)
            {
                std::span<const VriFenceSubmitDesc> waits;
                std::span<const VriFenceSubmitDesc> produced;
                if (i == 0)
                {
                    produced = std::span(signals).first(1);
                }
                else if (i < 3)
                {
                    waits    = std::span(signals).first(1);
                    produced = std::span(signals).subspan(i, 1);
                }
                else
                {
                    waits = std::span(signals).subspan(1);
                }
                auto submitted = frames[i]->submitWithCompletionExpected(waits, produced);
                if (!submitted)
                {
                    return std::unexpected(submitted.error());
                }
                if (i == 2)
                {
                    faults.heldFence = faults.lastSignals.back().fence;
                }
                completions.push_back(std::move(*submitted));
            }
            faults.pollResult     = VriResult_Timeout;
            const auto idleBefore = faults.idleCalls;
            if (!device.deferReleaseExpected(owner, completions) || owner || !completions.empty())
            {
                return std::unexpected("Resource ownership did not transfer with the complete accepted submission set");
            }
            auto pending = device.collectDeferredExpected();
            if (!pending || *pending || destroyed != cycle || faults.buffers != liveBuffers ||
                faults.memories != liveMemory)
            {
                return std::unexpected("A not-ready branch released native graph resources or their shared heap");
            }
            // An independent later completed batch must not wait behind the older unavailable observation.
            auto later = Frame::create(device);
            if (!later || !(*later)->beginExpected())
            {
                return std::unexpected("Prepare independent retirement batch failed");
            }
            auto complete = (*later)->submitWithCompletionExpected();
            if (!complete || !(*later)->waitExpected())
            {
                return std::unexpected("Submit independent retirement batch failed");
            }
            auto                              second = std::make_unique<Counted>(secondDestroyed);
            std::vector<SubmissionCompletion> after;
            after.push_back(std::move(*complete));
            if (!device.deferReleaseExpected(second, after))
            {
                return std::unexpected("Retire independent completed batch failed");
            }
            auto collected = device.collectDeferredExpected();
            if (!collected || *collected != 1 || secondDestroyed != cycle + 1 || destroyed != cycle)
            {
                return std::unexpected("A ready later release remained blocked behind another queue's observation");
            }
            faults.pollResult = VriResult_Failure;
            if (device.collectDeferredExpected() || destroyed != cycle || faults.buffers != liveBuffers)
            {
                return std::unexpected("Completion observation failure released pending resource ownership");
            }
            const auto beforeBegin = faults.beginCalls;
            if ((*later)->beginExpected() || faults.beginCalls != beforeBegin)
            {
                return std::unexpected("Frame begin ignored a deferred completion observation failure");
            }
            faults.pollResult = VriResult_Timeout;
            for (auto& frame : frames)
            {
                if (auto waited = frame->waitExpected(); !waited)
                {
                    return waited;
                }
                frame.reset();
            }
            if (!faults.fences.contains(faults.heldFence))
            {
                return std::unexpected("A completion credential expired with its source Frame");
            }
            for (size_t output = 0; output < outputs.size(); ++output)
            {
                const auto* values =
                    static_cast<const uint32_t*>(device.core.MapBuffer(outputs[output]->handle, 0, 1024));
                if (!values)
                {
                    return std::unexpected("Map retired graph's retained output failed");
                }
                bool matches = true;
                for (uint32_t i = 0; i < 256; ++i)
                {
                    matches &= values[i] == seed + (output == 2 ? 65536 : 0) + i;
                }
                device.core.UnmapBuffer(outputs[output]->handle);
                if (!matches)
                {
                    return std::unexpected("Retired multi-queue heap graph lost old or replacement GPU data");
                }
            }
            faults.pollResult = VriResult_Success;
            collected         = device.collectDeferredExpected();
            if (!collected || *collected != 1 || destroyed != cycle + 1 || !faults.buffers.empty() ||
                !faults.memories.empty() || faults.fences.contains(faults.heldFence) || faults.idleCalls != idleBefore)
            {
                return std::unexpected(
                    "Final completion did not release every graph object before heap memory without device idle");
            }
            faults.heldFence = nullptr;
        }
        std::puts("RESOURCE_HEAP_SUBMISSION dag_alias=1 scheduled_alias=1 conservative_disjoint=1 "
                  "missing_readers=2 alternate_route=1");
        std::puts("RESOURCE_RETIREMENT_GPU batches=3 destinations=4 exact_words=2304 owner_and_heap=retained "
                  "frame_independent=1 idle_waits=0");
        return {};
    }

    inline std::expected<void, std::string> boundaries(Device& device)
    {
        unsigned    destroyed = 0;
        Drain       drain {device};
        NativeFault faults(device);
        auto        frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto createdBefore = faults.createFenceCalls;
        if ((*frame)->submitWithCompletionExpected() || faults.createFenceCalls != createdBefore)
        {
            return std::unexpected("Invalid submission allocated or published a completion credential");
        }
        for (unsigned failure = 0; failure < 3; ++failure)
        {
            if (!(*frame)->beginExpected())
            {
                return std::unexpected("Begin completion rejection control failed");
            }
            const auto fences = faults.fences;
            faults.failCreate = failure == 0;
            faults.failEnd    = failure == 1;
            faults.failSubmit = failure == 2;
            auto rejected     = (*frame)->submitWithCompletionExpected();
            faults.failCreate = false;
            faults.failEnd    = false;
            faults.failSubmit = false;
            if (rejected || faults.fences != fences || !(*frame)->discardExpected())
            {
                return std::unexpected("Rejected create/end/submit leaked a credential or prevented command discard");
            }
        }
        auto                              resource = std::make_unique<Counted>(destroyed);
        std::vector<SubmissionCompletion> none;
        std::vector<SubmissionCompletion> invalid(1);
        if (device.deferReleaseExpected(resource, none) || !resource ||
            device.deferReleaseExpected(resource, invalid) || !resource || invalid.size() != 1 ||
            invalid.front().readyExpected())
        {
            return std::unexpected("Missing completion data consumed ownership or fabricated readiness");
        }
        auto foreignDevice = Device::create(true, nullptr, 0);
        if (!foreignDevice)
        {
            return std::unexpected(foreignDevice.error());
        }
        auto foreignFrame = Frame::create(**foreignDevice);
        if (!foreignFrame || !(*foreignFrame)->beginExpected())
        {
            return std::unexpected("Begin foreign completion control failed");
        }
        auto foreign = (*foreignFrame)->submitWithCompletionExpected();
        if (!foreign)
        {
            return std::unexpected(foreign.error());
        }
        std::vector<SubmissionCompletion> foreignUses;
        foreignUses.push_back(std::move(*foreign));
        if (device.deferReleaseExpected(resource, foreignUses) || !resource || foreignUses.size() != 1 ||
            !(*foreignFrame)->waitExpected())
        {
            return std::unexpected("Foreign completion accepted or consumed local ownership");
        }
        if (!(*frame)->beginExpected())
        {
            return std::unexpected("Begin completion retry failed");
        }
        auto accepted = (*frame)->submitWithCompletionExpected();
        if (!accepted)
        {
            return std::unexpected(accepted.error());
        }
        faults.heldFence  = faults.lastSignals.back().fence;
        faults.pollResult = VriResult_Timeout;
        SubmissionCompletion moved(std::move(*accepted));
        SubmissionCompletion transferred;
        transferred = std::move(moved);
        // A moved-from completion is explicitly required to reject observation as empty.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if (accepted->readyExpected() || moved.readyExpected())
        {
            return std::unexpected("Moved-from submission credentials retained a native fence");
        }
        auto pending = transferred.readyExpected();
        if (!pending || *pending)
        {
            return std::unexpected("Submission timeout was not a non-ready observation");
        }
        std::vector<SubmissionCompletion> lastUses;
        lastUses.push_back(std::move(transferred));
        if (!device.deferReleaseExpected(resource, lastUses) || resource || !lastUses.empty())
        {
            return std::unexpected("Valid retirement ownership did not transfer on retry");
        }
        if (!(*frame)->waitExpected())
        {
            return std::unexpected("Wait completion retry failed");
        }
        frame->reset();
        if (destroyed || !faults.fences.contains(faults.heldFence))
        {
            return std::unexpected("Destroying Frame also destroyed a retained credential or owner");
        }
        faults.pollResult = VriResult_Success;
        auto nextFrame    = Frame::create(device);
        if (!nextFrame || !(*nextFrame)->beginExpected() || destroyed != 1 || !(*nextFrame)->discardExpected() ||
            faults.fences.contains(faults.heldFence) || faults.idleCalls)
        {
            return std::unexpected("Frame begin did not collect a ready deferred owner without device idle");
        }
        faults.heldFence = nullptr;
        std::puts("RESOURCE_RETIREMENT_BOUNDARIES rejected=3 foreign=rejected moves=checked frame_begin=collects "
                  "idle_waits=0");
        return {};
    }

    inline std::expected<void, std::string> probe(Device& device)
    {
        if (auto result = boundaries(device); !result)
        {
            return result;
        }
        return graphLifetime(device);
    }
} // namespace vultra::test::resource_retirement_probe
