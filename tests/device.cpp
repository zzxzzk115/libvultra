#include <vultra/drivers/rhi/device.hpp>

#include <array>
#include <cstdio>
#include <span>
#include <string_view>

namespace
{
    using namespace vultra;
    constexpr uint64_t timeoutNs = 5'000'000'000;

    struct RestoreCore
    {
        Device&          device;
        VriCoreInterface previous;

        ~RestoreCore()
        {
            device.core = previous;
        }
    };

    bool failure(const std::expected<void, std::string>& result, std::string_view operation)
    {
        return !result && result.error().starts_with(operation);
    }

    std::expected<void, std::string> completionFailures(Device& device)
    {
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        if ((*frame)->submitExpected() || !(*frame)->waitExpected() || !(*frame)->beginExpected() ||
            (*frame)->beginExpected() || (*frame)->waitExpected())
        {
            return std::unexpected("Frame accepted an invalid recording/submission phase");
        }
        {
            RestoreCore restore {device, device.core};
            device.core.EndCommandBuffer = [](VriCommandBuffer*)
            {
                return VriResult_DeviceLost;
            };
            if (!failure((*frame)->submitExpected(), "End command buffer") || (*frame)->submitExpected() ||
                (*frame)->beginExpected())
            {
                return std::unexpected("Failed end did not invalidate the recording");
            }
        }
        if (!(*frame)->discardExpected() || !(*frame)->beginExpected())
        {
            return std::unexpected("Cannot discard and retry a failed recording");
        }
        {
            RestoreCore     restore {device, device.core};
            static unsigned waits    = 0;
            waits                    = 0;
            device.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
            {
                ++waits;
                return VriResult_Timeout;
            };
            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc* submit)
            {
                return submit->signalFenceNum == 1 && submit->signalFences[0].value == 1 ? VriResult_InvalidArgument :
                                                                                           VriResult_Failure;
            };
            const auto failed = (*frame)->submitAndWaitExpected();
            if (failed || failed.error() != "Submit frame failed: " + std::to_string(VriResult_InvalidArgument) ||
                waits != 0)
            {
                return std::unexpected("Failed submission waited or advanced its completion value");
            }
        }
        {
            RestoreCore restore {device, device.core};
            // Retry must reuse the executable command buffer, not end it again.
            device.core.EndCommandBuffer = [](VriCommandBuffer*)
            {
                return VriResult_Failure;
            };
            if (auto submitted = (*frame)->submitExpected(); !submitted)
            {
                return submitted;
            }
        }
        if ((*frame)->beginExpected() || (*frame)->discardExpected() || (*frame)->submitExpected())
        {
            return std::unexpected("Pending submission permitted reset, discard or resubmit");
        }
        {
            RestoreCore restore {device, device.core};
            device.core.WaitForFence = [](VriFence*, uint64_t value, uint64_t)
            {
                return value == 1 ? VriResult_Timeout : VriResult_Failure;
            };
            if (!failure((*frame)->waitExpected(0),
                         "Wait for frame completion failed: " + std::to_string(VriResult_Timeout)) ||
                (*frame)->discardExpected())
            {
                return std::unexpected("Wait timeout lost the pending submission");
            }
        }
        if (!(*frame)->waitExpected(timeoutNs) || !(*frame)->beginExpected())
        {
            return std::unexpected("Cannot complete a timed-out submission and reuse its allocator");
        }
        const VriFenceSubmitDesc                  nullFence {};
        const std::span<const VriFenceSubmitDesc> invalid(&nullFence, 1);
        if (!failure((*frame)->submitExpected(invalid), "Frame wait fence is null") ||
            !failure((*frame)->submitExpected({}, invalid), "Frame signal fence is null") ||
            !(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Invalid external fences did not preserve the recording");
        }
        {
            RestoreCore restore {device, device.core};
            device.core.BeginCommandBuffer = [](VriCommandBuffer*)
            {
                return VriResult_DeviceLost;
            };
            const auto begin = (*frame)->beginExpected();
            if (begin || !begin.error().starts_with("Begin command buffer"))
            {
                return std::unexpected("Begin failure was not propagated");
            }
        }
        if (!(*frame)->discardExpected() || !(*frame)->beginExpected() || !(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Cannot recover from a failed begin");
        }
        {
            RestoreCore restore {device, device.core};
            device.core.DeviceWaitIdle = [](VriDevice*)
            {
                return VriResult_DeviceLost;
            };
            if (!failure(device.waitIdleExpected(), "Wait for device idle"))
            {
                return std::unexpected("Device completion error was ignored");
            }
        }
        return device.waitIdleExpected();
    }

    std::expected<void, std::string> frameLifetimes(Device& device)
    {
        RestoreCore     restore {device, device.core};
        static unsigned idleCalls  = 0;
        idleCalls                  = 0;
        device.core.DeviceWaitIdle = [](VriDevice*)
        {
            ++idleCalls;
            return VriResult_Success;
        };
        for (unsigned step = 0; step < 4; ++step)
        {
            const auto previous = device.core;
            if (step == 0)
            {
                device.core.CreateCommandAllocator = [](VriDevice*, VriQueueType, VriCommandAllocator**)
                {
                    return VriResult_DeviceLost;
                };
            }
            else if (step == 1)
            {
                device.core.CreateCommandBuffer = [](VriCommandAllocator*, VriCommandBuffer**)
                {
                    return VriResult_DeviceLost;
                };
            }
            else if (step == 2)
            {
                device.core.CreateFence = [](VriDevice*, uint64_t, VriFence**)
                {
                    return VriResult_DeviceLost;
                };
            }
            auto frame  = Frame::create(device);
            device.core = previous;
            if ((step < 3 && frame) || (step == 3 && !frame))
            {
                return std::unexpected("Frame partial initialization did not preserve its failure");
            }
        }
        if (Frame::create(device, VriQueueType_Count))
        {
            return std::unexpected("Frame accepted an invalid queue type");
        }
        static auto nativeWait   = device.core.WaitForFence;
        nativeWait               = device.core.WaitForFence;
        static unsigned ownWaits = 0;
        ownWaits                 = 0;
        device.core.WaitForFence = [](VriFence* fence, uint64_t value, uint64_t timeout)
        {
            ++ownWaits;
            return value == 1 ? nativeWait(fence, value, timeout) : VriResult_InvalidArgument;
        };
        {
            auto submitted = Frame::create(device);
            if (!submitted || !(*submitted)->beginExpected() || !(*submitted)->submitExpected())
            {
                return std::unexpected("Cannot prepare a submitted Frame destructor check");
            }
            // Destruction establishes this Frame's own completion without waiting for other queues.
        }
        if (idleCalls != 0 || ownWaits != 1)
        {
            return std::unexpected("Frame cleanup did not wait for only its own submitted completion");
        }
        // Exercise the immediate-backend lifetime decision without claiming native OpenGL GPU coverage.
        static VriDeviceDesc immediate {};
        immediate                 = *device.core.GetDeviceDesc(device.handle);
        immediate.graphicsAPI     = VriGraphicsAPI_OpenGL;
        device.core.GetDeviceDesc = [](const VriDevice*) -> const VriDeviceDesc*
        {
            return &immediate;
        };
        {
            auto recorded = Frame::create(device);
            if (!recorded || !(*recorded)->beginExpected() || (*recorded)->discardExpected())
            {
                return std::unexpected("Immediate commands were incorrectly accepted for discard");
            }
        }
        if (idleCalls != 1 || ownWaits != 1)
        {
            return std::unexpected("Immediate recording destruction lost its completion boundary");
        }
        return {};
    }

    std::expected<void, std::string> allocatorReset(Device& device)
    {
        struct Allocation
        {
            Device&              device;
            VriCommandAllocator* handle = nullptr;

            ~Allocation()
            {
                if (handle)
                {
                    device.core.DestroyCommandAllocator(handle);
                }
            }
        };

        Allocation first {device};
        Allocation second {device};
        auto&      core = device.core;
        if (core.CreateCommandAllocator(device.handle, VriQueueType_Graphics, &first.handle) != VriResult_Success ||
            core.CreateCommandAllocator(device.handle, VriQueueType_Graphics, &second.handle) != VriResult_Success)
        {
            return std::unexpected("Create allocator ownership test resources failed");
        }
        std::array<VriCommandBuffer*, 2> commands {};
        VriCommandBuffer*                other = nullptr;
        if (core.CreateCommandBuffer(first.handle, &commands[0]) != VriResult_Success ||
            core.CreateCommandBuffer(first.handle, &commands[1]) != VriResult_Success ||
            core.CreateCommandBuffer(second.handle, &other) != VriResult_Success)
        {
            return std::unexpected("Create allocator ownership command buffers failed");
        }
        for (auto* command : commands)
        {
            if (core.BeginCommandBuffer(command) != VriResult_Success)
            {
                return std::unexpected("Begin allocator reset test failed");
            }
        }
        if (core.BeginCommandBuffer(other) != VriResult_Success)
        {
            return std::unexpected("Begin independent allocator test failed");
        }
        core.ResetCommandAllocator(first.handle);
        for (auto* command : commands)
        {
            if (core.BeginCommandBuffer(command) != VriResult_Success ||
                core.EndCommandBuffer(command) != VriResult_Success)
            {
                return std::unexpected("Allocator reset did not reset every owned recording");
            }
        }
        if (core.EndCommandBuffer(other) != VriResult_Success)
        {
            return std::unexpected("Allocator reset changed another allocator's recording");
        }
        return {};
    }

    std::expected<void, std::string> run()
    {
        auto device = Device::create();
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto&                    gpu = **device;
        VriCoordinateConventions authored {};
        if (vriGetCoordinateConventions(gpu.handle, &authored) != VriResult_Success ||
            authored.clipSpaceY != VriClipSpaceY_Up)
        {
            return std::unexpected("Builtin authoring convention changed");
        }
        if (auto completed = completionFailures(gpu); !completed)
        {
            return completed;
        }
        if (auto checked = frameLifetimes(gpu); !checked)
        {
            return checked;
        }
        if (auto checked = allocatorReset(gpu); !checked)
        {
            return checked;
        }
        auto desc                      = vriDefaultDeviceCreationDesc();
        desc.graphicsAPI               = gpu.core.GetDeviceDesc(gpu.handle)->graphicsAPI;
        desc.enableValidation          = true;
        desc.coordinates.textureOrigin = VriTextureOrigin_BottomLeft;
        auto configured                = Device::create(desc);
        if (!configured)
        {
            return std::unexpected(configured.error());
        }
        VriCoordinateConventions actual {};
        if (vriGetCoordinateConventions((*configured)->handle, &actual) != VriResult_Success ||
            actual.clipSpaceY != VriClipSpaceY_Down || actual.textureOrigin != VriTextureOrigin_BottomLeft)
        {
            return std::unexpected("Device ownership wrapper overwrote requested conventions");
        }
        return {};
    }
} // namespace

int main()
{
    if (auto result = run(); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Device configuration, Frame failure recovery and completion lifetimes passed");
    return 0;
}
