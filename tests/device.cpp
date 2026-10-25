#include <vultra/drivers/rhi/device.hpp>

#include <functional>
#include <iostream>
#include <string_view>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void expectFailure(const char* operation, const std::function<void()>& action)
    {
        try
        {
            action();
        }
        catch (const std::runtime_error& error)
        {
            require(std::string_view(error.what()).starts_with(operation), "Failure lost its operation");
            return;
        }
        throw std::runtime_error("GPU operation failure was ignored");
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
        vultra::Frame frame(device);
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
            frame.begin();
            expectFailure("Submit frame",
                          [&]
                          {
                              frame.submitAndWait();
                          });
            require(waitCalls == 0, "Failed submission must not wait for an unsignaled fence");

            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                return VriResult_Success;
            };
            frame.begin();
            expectFailure("Wait for frame completion",
                          [&]
                          {
                              frame.submitAndWait();
                          });
            require(waitCalls == 1, "Frame completion must use the status-returning wait");

            device.core.DeviceWaitIdle = [](VriDevice*)
            {
                return VriResult_DeviceLost;
            };
            expectFailure("Wait for device idle",
                          [&]
                          {
                              device.waitIdle();
                          });
        }
        // Fault injection submits no GPU work. Restore the real table before retry and destruction.
        frame.begin();
        frame.submitAndWait();
        device.waitIdle();
    }
} // namespace

int main()
try
{
    vultra::Device           device;
    VriCoordinateConventions authored {};
    vultra::check(vriGetCoordinateConventions(device.handle, &authored), "Query builtin conventions");
    require(authored.clipSpaceY == VriClipSpaceY_Up, "Builtin authoring convention changed");
    completionFailures(device);

    auto desc                      = vriDefaultDeviceCreationDesc();
    desc.graphicsAPI               = device.core.GetDeviceDesc(device.handle)->graphicsAPI;
    desc.enableValidation          = true;
    desc.coordinates.textureOrigin = VriTextureOrigin_BottomLeft;
    vultra::Device           configured(desc);
    VriCoordinateConventions actual {};
    vultra::check(vriGetCoordinateConventions(configured.handle, &actual), "Query requested conventions");
    require(actual.clipSpaceY == VriClipSpaceY_Down && actual.textureOrigin == VriTextureOrigin_BottomLeft,
            "Device ownership wrapper overwrote requested conventions");
    std::cout << "Device configuration and completion failures passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
