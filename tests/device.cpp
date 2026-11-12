#include "support/check.hpp"

#include <vultra/gpu/device.hpp>
#include <vultra/gpu/resources.hpp>

#include <format>
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

    std::string describe(const vultra::DeviceFeatures& features)
    {
        return std::format("async compute {}, ray tracing pipeline {}, ray query {}, mesh shading {}, draw indirect "
                           "count {}, variable-rate shading {}, multiview {} ({} views), bindless {}, wave "
                           "operations {}, block compression {}",
                           features.asyncCompute,
                           features.rayTracingPipeline,
                           features.rayQuery,
                           features.meshShading,
                           features.drawIndirectCount,
                           features.variableRateShading,
                           features.multiview,
                           features.maxViews,
                           features.bindless,
                           features.waveOperations,
                           features.blockCompression);
    }

    // Optional features are reported as the device granted them, so a program learns what it may use; one that
    // was not requested is not reported on Vulkan, where requesting is what enables it.
    void features(vultra::Device& plain)
    {
        const auto api      = plain.core.GetDeviceDesc(plain.handle)->graphicsAPI;
        const auto features = plain.features();
        require(features.asyncCompute == (plain.computeQueue != nullptr),
                "Async compute must mean a compute queue separate from the graphics queue");
        if (api == VriGraphicsAPI_Vulkan)
        {
            require(!features.meshShading && !features.rayQuery && !features.rayTracingPipeline &&
                        !features.variableRateShading && !features.bindless,
                    "A Vulkan device reported an optional feature nobody requested");
        }

        constexpr uint64_t requested = VriFeature_MeshShader | VriFeature_RayQuery | VriFeature_RayTracing |
                                       VriFeature_VariableShadingRate | VriFeature_Bindless;
        const auto rich     = must(vultra::Device::create(true, nullptr, requested, true));
        const auto granted  = rich->core.GetDeviceDesc(rich->handle)->enabledFeatures;
        const auto reported = rich->features();
        require(reported.meshShading == ((granted & VriFeature_MeshShader) != 0) &&
                    reported.rayQuery == ((granted & VriFeature_RayQuery) != 0) &&
                    reported.rayTracingPipeline == ((granted & VriFeature_RayTracing) != 0) &&
                    reported.variableRateShading == ((granted & VriFeature_VariableShadingRate) != 0) &&
                    reported.bindless == ((granted & VriFeature_Bindless) != 0),
                "A requested feature is reported differently from what the device granted");
        require(!reported.multiview || reported.maxViews >= 2, "Multiview without room for two views");

        // Block compression is reported only when a BC7 texture can be created and sampled.
        if (reported.blockCompression)
        {
            auto desc  = vultra::colorTexture({8, 8}, VriFormat_BC7_UNORM);
            desc.usage = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
            auto bc7   = must(vultra::Texture::create(*rich, desc));
            static_cast<void>(must(bc7.view()));
        }
        std::cout << "Features of this device when nothing is requested: " << describe(features) << '\n'
                  << "Features granted when every optional one is requested: " << describe(reported) << '\n';
    }
} // namespace

int main()
{
    const auto               device = must(vultra::Device::create());
    VriCoordinateConventions authored {};
    must(vultra::check(vriGetCoordinateConventions(device->handle, &authored), "Query builtin conventions"));
    require(authored.clipSpaceY == VriClipSpaceY_Up, "Builtin authoring convention changed");
    completionFailures(*device);
    features(*device);

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
