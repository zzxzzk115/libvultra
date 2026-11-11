#include "support/check.hpp"

#include <vultra/gpu/device.hpp>

#include <iostream>
#include <string_view>

namespace
{
    using support::must;
    using support::require;

    void expectFailure(const char* operation, const std::expected<void, vultra::Error>& result)
    {
        require(!result, "GPU operation failure was ignored");
        require(std::string_view(result.error().message).starts_with(operation), "Failure lost its operation");
    }

    struct RestoreCore
    {
        vultra::Device&  device;
        VriCoreInterface previous;

        ~RestoreCore()
        {
            device.core = previous;
        }
    };

    void completionFailures(vultra::Device& device)
    {
        const auto frame = must(vultra::Frame::create(device));
        {
            RestoreCore     restore {device, device.core};
            static uint32_t waitCalls = 0;
            waitCalls                 = 0;
            device.core.WaitForFence  = [](VriFence*, uint64_t, uint64_t)
            {
                ++waitCalls;
                return VriResult_Timeout;
            };
            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                return VriResult_InvalidArgument;
            };
            must(frame->begin());
            expectFailure("Submit frame", frame->submitAndWait());
            require(waitCalls == 0, "Failed submission must not wait for an unsignaled fence");

            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                return VriResult_Success;
            };
            must(frame->begin());
            expectFailure("Wait for frame completion", frame->submitAndWait());
            require(waitCalls == 1, "Frame completion must use the status-returning wait");

            device.core.DeviceWaitIdle = [](VriDevice*)
            {
                return VriResult_DeviceLost;
            };
            expectFailure("Wait for device idle", device.waitIdle());
        }
        // Fault injection submits no GPU work. Restore the real table before retry and destruction.
        must(frame->begin());
        must(frame->submitAndWait());
        must(device.waitIdle());
    }
} // namespace

int main()
{
    const auto               device = must(vultra::Device::create());
    VriCoordinateConventions authored {};
    must(vultra::check(vriGetCoordinateConventions(device->handle, &authored), "Query builtin conventions"));
    require(authored.clipSpaceY == VriClipSpaceY_Up, "Builtin authoring convention changed");
    completionFailures(*device);

    auto desc                           = vriDefaultDeviceCreationDesc();
    desc.graphicsAPI                    = device->core.GetDeviceDesc(device->handle)->graphicsAPI;
    desc.enableValidation               = true;
    desc.coordinates.textureOrigin      = VriTextureOrigin_BottomLeft;
    const auto               configured = must(vultra::Device::create(desc));
    VriCoordinateConventions actual {};
    must(vultra::check(vriGetCoordinateConventions(configured->handle, &actual), "Query requested conventions"));
    require(actual.clipSpaceY == VriClipSpaceY_Down && actual.textureOrigin == VriTextureOrigin_BottomLeft,
            "Device ownership wrapper overwrote requested conventions");
    std::cout << "Device configuration and completion failures passed\n";
    return 0;
}
