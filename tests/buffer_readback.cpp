#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;

    struct Faults
    {
        Device&               device;
        VriCoreInterface      original;
        bool                  failAllocation     = false;
        bool                  timeout            = false;
        bool                  failMap            = false;
        bool                  completed          = false;
        bool                  mappedEarly        = false;
        bool                  monitorDestruction = false;
        bool                  destroyedEarly     = false;
        uint32_t              waits              = 0;
        uint32_t              barriers           = 0;
        uint32_t              rejectBarrier      = 0;
        VriBuffer*            observedSource     = nullptr;
        uint64_t              sourceSize         = 0;
        uint64_t              selectedOffset     = 0;
        uint64_t              selectedSize       = 0;
        bool                  touchedOutside     = false;
        std::set<VriBuffer*>  staging;
        inline static Faults* s_Active = nullptr;

        explicit Faults(Device& owner) :
            device(owner),
            original(owner.core)
        {
            s_Active                          = this;
            device.core.CreateBuffer          = create;
            device.core.DestroyBuffer         = destroy;
            device.core.MapBuffer             = map;
            device.core.WaitForFence          = wait;
            device.core.CmdBufferBarrierRange = barrier;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL create(VriDevice* owner, const VriBufferDesc* desc, VriBuffer** output)
        {
            auto& self = *s_Active;
            if (self.failAllocation && desc->memoryLocation == VriMemoryLocation_HostReadback)
            {
                *output = nullptr;
                return VriResult_OutOfMemory;
            }
            const auto result = self.original.CreateBuffer(owner, desc, output);
            if (result == VriResult_Success && *output && desc->memoryLocation == VriMemoryLocation_HostReadback)
            {
                self.staging.insert(*output);
            }
            return result;
        }

        static void VRI_CALL destroy(VriBuffer* buffer)
        {
            auto& self = *s_Active;
            if (self.staging.erase(buffer) && self.monitorDestruction && !self.completed)
            {
                self.destroyedEarly = true;
            }
            self.original.DestroyBuffer(buffer);
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            auto& self = *s_Active;
            if (self.staging.contains(buffer))
            {
                self.mappedEarly |= !self.completed;
                if (self.failMap)
                {
                    return nullptr;
                }
            }
            return self.original.MapBuffer(buffer, offset, size);
        }

        static VriResult VRI_CALL wait(VriFence* fence, uint64_t value, uint64_t timeoutNs)
        {
            auto& self = *s_Active;
            ++self.waits;
            if (self.timeout)
            {
                return VriResult_Timeout;
            }
            const auto result = self.original.WaitForFence(fence, value, timeoutNs);
            self.completed |= result == VriResult_Success;
            return result;
        }

        static VriResult VRI_CALL barrier(VriCommandBuffer* commands, const VriBufferBarrierDesc* desc)
        {
            auto& self = *s_Active;
            if (desc->buffer == self.observedSource)
            {
                const auto size = desc->size ? desc->size : self.sourceSize - desc->offset;
                self.touchedOutside |=
                    desc->offset < self.selectedOffset || desc->offset + size > self.selectedOffset + self.selectedSize;
            }
            if (++self.barriers == self.rejectBarrier)
            {
                return VriResult_Failure;
            }
            return self.original.CmdBufferBarrierRange(commands, desc);
        }
    };

    std::array<std::byte, 1024> pattern(uint32_t seed)
    {
        std::array<std::byte, 1024> bytes {};
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = std::byte((i * 37 + (i / 31) * 13 + seed * 53) & 255);
        }
        return bytes;
    }

    Result equal(const BufferReadbackData& data, std::span<const std::byte> expected, uint64_t offset, uint64_t size)
    {
        if (data.offset != offset || data.bytes.size() != size ||
            std::memcmp(data.bytes.data(), expected.data() + offset, size_t(size)) != 0)
        {
            return std::unexpected("Completed buffer readback differs from the independent byte pattern");
        }
        return {};
    }

    Result replace(Device& device, Buffer& destination, Buffer& source)
    {
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
        auto state = destination.captureState();
        if (auto ready =
                destination.transitionExpected(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !ready)
        {
            return ready;
        }
        const VriBufferCopyDesc copy {0, 0, destination.desc.size};
        device.core.CmdCopyBuffer(*commands, destination.handle, source.handle, &copy);
        if (auto restored = destination.restoreExpected(*commands, state); !restored)
        {
            return restored;
        }
        return (*frame)->submitAndWaitExpected();
    }

    Result crossQueue(Device& device, Buffer& destination, Faults& faults)
    {
        struct Fences
        {
            Device&                  device;
            std::array<VriFence*, 2> handles {};

            ~Fences()
            {
                for (auto* fence : handles)
                {
                    if (fence)
                    {
                        device.core.DestroyFence(fence);
                    }
                }
            }
        } fences {device};

        for (auto& fence : fences.handles)
        {
            if (device.core.CreateFence(device.handle, 0, &fence) != VriResult_Success || !fence)
            {
                return std::unexpected("Create explicit buffer readback dependency fence failed");
            }
        }
        const auto expected     = pattern(2);
        auto       producerData = uploadBufferExpected(device,
                                                 expected,
                                                 VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                                       {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        auto       producer     = Frame::create(device, VriQueueType_Compute);
        if (!producerData || !producer)
        {
            return std::unexpected("Create compute-queue readback producer failed");
        }
        auto commands = (*producer)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto state = destination.captureState();
        if (auto ready =
                destination.transitionExpected(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !ready)
        {
            return ready;
        }
        const VriBufferCopyDesc copy {0, 0, destination.desc.size};
        device.core.CmdCopyBuffer(*commands, destination.handle, (*producerData)->handle, &copy);
        if (auto restored = destination.restoreExpected(*commands, state); !restored)
        {
            return restored;
        }
        const std::array writerSignal {VriFenceSubmitDesc {fences.handles[0], 1, VriPipelineStage_AllCommands}};
        if (auto submitted = (*producer)->submitExpected({}, writerSignal); !submitted)
        {
            return submitted;
        }
        const std::array readerSignal {VriFenceSubmitDesc {fences.handles[1], 1, VriPipelineStage_AllCommands}};
        const auto       beforeWaits = faults.waits;
        auto             task        = BufferReadback::start(device, destination, 333, 407, writerSignal, readerSignal);
        if (!task || faults.waits != beforeWaits)
        {
            return std::unexpected("Explicit cross-queue readback failed or waited on CPU before returning");
        }
        auto data = (*task)->dataExpected();
        if (!data || !equal(*data, expected, 333, 407) || destination.captureState() != state ||
            device.core.WaitForFence(fences.handles[1], 1, 0) != VriResult_Success)
        {
            return std::unexpected("Cross-queue buffer copy lost bytes, state or caller completion signal");
        }
        return (*producer)->waitExpected();
    }

    Result run(Device& device)
    {
        const auto     first  = pattern(0);
        const auto     second = pattern(1);
        constexpr auto usage =
            VriBufferUsage_TransferSrc | VriBufferUsage_StorageBuffer | VriBufferUsage_ConcurrentQueues;
        auto source = uploadBufferExpected(device, first, usage, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        auto other = uploadBufferExpected(device, second, usage, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        auto frame = Frame::create(device);
        if (!source || !other || !frame)
        {
            return std::unexpected("Create buffer readback GPU inputs failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto ready = (*source)->transitionExpected(*commands,
                                                       {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader},
                                                       128,
                                                       256);
            !ready)
        {
            return ready;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        const auto original = (*source)->captureState();
        if ((*source)->stateExpected())
        {
            return std::unexpected("Readback fixture requires mixed source access states");
        }
        Faults faults(device);
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto beforeGuards = faults.barriers;
        if ((*source)->restoreExpected(nullptr, original) ||
            (*source)->restoreExpected(*commands, (*other)->captureState()) ||
            (*source)->restoreExpected(*commands, original, 1024) ||
            (*source)->restoreExpected(*commands, original, 1, UINT64_MAX) || (*source)->captureState() != original ||
            faults.barriers != beforeGuards)
        {
            return std::unexpected("Invalid buffer restoration changed state or recorded native barriers");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        const std::array<std::pair<uint64_t, uint64_t>, 6> ranges {
            {{0, 0}, {17, 521}, {513, 0}, {0, 32}, {1023, 1}, {129, 3}}};
        uint64_t checkedBytes = 0;
        for (const auto& [offset, count] : ranges)
        {
            const auto size = count ? count : first.size() - offset;
            auto       task = BufferReadback::create(device, **source, offset, count);
            if (!task || (*task)->dataExpected() || (*task)->waitExpected() ||
                (*task)->recordExpected(nullptr, **source))
            {
                return std::unexpected("External buffer readback accepted unrecorded data or invalid completion");
            }
            commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            faults.observedSource = (*source)->handle;
            faults.sourceSize     = (*source)->desc.size;
            faults.selectedOffset = offset;
            faults.selectedSize   = size;
            auto recorded         = (*task)->recordExpected(*commands, **source);
            faults.observedSource = nullptr;
            if (!recorded)
            {
                return recorded;
            }
            if ((*source)->captureState() != original || faults.touchedOutside)
            {
                return std::unexpected("Buffer readback did not preserve mixed byte-range state");
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            for (unsigned repeat = 0; repeat < 2; ++repeat)
            {
                auto data = (*task)->dataExpected();
                if (!data)
                {
                    return std::unexpected(data.error());
                }
                if (auto checked = equal(*data, first, offset, size); !checked)
                {
                    return checked;
                }
                checkedBytes += size;
            }
        }
        const auto live = faults.staging.size();
        if (BufferReadback::create(device, **source, 1024) || BufferReadback::create(device, **source, 1, 1024) ||
            BufferReadback::create(device, **source, UINT64_MAX, 1) ||
            BufferReadback::create(device, **source, 1, UINT64_MAX) || faults.staging.size() != live)
        {
            return std::unexpected("Invalid buffer readback range allocated or succeeded");
        }
        const auto savedUsage = (*source)->desc.usage;
        (*source)->desc.usage &= ~VriBufferUsage_TransferSrc;
        auto unsupported      = BufferReadback::create(device, **source);
        (*source)->desc.usage = savedUsage;
        const auto savedSize  = (*source)->desc.size;
        (*source)->desc.size  = UINT64_MAX;
        auto oversized        = BufferReadback::create(device, **source);
        (*source)->desc.size  = savedSize;
        if (unsupported || oversized || faults.staging.size() != live)
        {
            return std::unexpected("Unsupported buffer readback storage was accepted");
        }
        auto foreign = Device::create(true);
        if (!foreign || BufferReadback::create(**foreign, **source))
        {
            return std::unexpected("Buffer readback did not reject a foreign device");
        }
        for (unsigned failure = 0; failure < 4; ++failure)
        {
            const auto core       = device.core;
            faults.failAllocation = failure == 0;
            if (failure == 1)
            {
                device.core.BeginCommandBuffer = [](VriCommandBuffer*)
                {
                    return VriResult_Failure;
                };
            }
            if (failure == 2)
            {
                device.core.EndCommandBuffer = [](VriCommandBuffer*)
                {
                    return VriResult_Failure;
                };
            }
            if (failure == 3)
            {
                device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
                {
                    return VriResult_Failure;
                };
            }
            auto rejected         = BufferReadback::start(device, **source, 17, 521);
            device.core           = core;
            faults.failAllocation = false;
            if (rejected || faults.staging.size() != live || (*source)->captureState() != original)
            {
                return std::unexpected("Rejected buffer readback lost source state or staging ownership");
            }
        }
        auto task = BufferReadback::create(device, **source, 17, 521);
        if (!task)
        {
            return std::unexpected(task.error());
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*task)->recordExpected(*commands, **source); !recorded)
        {
            return recorded;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        if (auto changed = replace(device, **source, **other); !changed)
        {
            return changed;
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        faults.barriers      = 0;
        faults.rejectBarrier = 6;
        auto rejected        = (*task)->recordExpected(*commands, **source);
        faults.rejectBarrier = 0;
        if (rejected || (*source)->captureState() != original)
        {
            return std::unexpected("Late buffer restoration failure did not restore source bookkeeping");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        auto previous = (*task)->dataExpected();
        if (!previous || !equal(*previous, first, 17, 521))
        {
            return std::unexpected("Discarded readback changed its previously completed bytes");
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*task)->recordExpected(*commands, **source); !recorded)
        {
            return recorded;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        auto recovered = (*task)->dataExpected();
        if (!recovered || !equal(*recovered, second, 17, 521))
        {
            return std::unexpected("Buffer readback did not recover after command discard");
        }
        task->reset();
        faults.completed       = false;
        const auto beforeWaits = faults.waits;
        auto       pending     = BufferReadback::start(device, **source, 129, 3);
        if (!pending || faults.waits != beforeWaits || (*source)->captureState() != original)
        {
            return std::unexpected("Starting buffer readback waited on CPU or changed source state");
        }
        faults.timeout = true;
        auto timedOut  = (*pending)->dataExpected(0);
        faults.timeout = false;
        if (timedOut || faults.completed || faults.mappedEarly || faults.staging.empty())
        {
            return std::unexpected("Timed-out buffer readback mapped or released pending storage");
        }
        faults.failMap = true;
        auto failedMap = (*pending)->dataExpected();
        faults.failMap = false;
        if (failedMap || !faults.completed)
        {
            return std::unexpected("Expected buffer map failure was lost");
        }
        recovered = (*pending)->dataExpected();
        if (!recovered || !equal(*recovered, second, 129, 3) || (*pending)->recordExpected(nullptr, **source))
        {
            return std::unexpected("Owned buffer readback failed map retry or allowed external recording");
        }
        pending->reset();
        faults.completed          = false;
        faults.monitorDestruction = true;
        pending                   = BufferReadback::start(device, **source, 513);
        if (!pending)
        {
            return std::unexpected(pending.error());
        }
        pending->reset();
        faults.monitorDestruction = false;
        if (faults.destroyedEarly || !faults.completed || !faults.staging.empty() || faults.mappedEarly)
        {
            return std::unexpected("Buffer readback released storage before observed GPU completion");
        }
        if (auto chained = crossQueue(device, **source, faults); !chained)
        {
            return chained;
        }
        std::printf(
            "BUFFER_READBACK exact_bytes=%llu mixed_ranges=1 selected_scope=1 record_recovery=1 timeout_map_retry=1 "
            "completion_owner=1 explicit_queue_chain=1\n",
            static_cast<unsigned long long>(checkedBytes + 521 * 2 + 3 + 407));
        return {};
    }
} // namespace

int main(int, char**)
{
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    auto checked = run(**device);
    if (!checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
