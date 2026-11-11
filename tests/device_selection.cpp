#include "support/check.hpp"

#include <vultra/gpu/device.hpp>

#include <iostream>
#include <memory>
#include <vector>

int main()
{
    using support::must;
    using support::require;
    {
        const auto device = must(vultra::Device::create());
        require(device->queue != nullptr, "Automatic GPU selection did not provide a graphics queue");
        const auto frame = must(vultra::Frame::create(*device));
        must(frame->begin());
        must(frame->submitAndWait());
    }

    // Discover whether this machine can exercise the original Ray Query regression.
    constexpr uint64_t          required = VriFeature_RayQuery | VriFeature_Bindless;
    std::vector<VriAdapterDesc> adapters(vriEnumerateAdapters(VriGraphicsAPI_Vulkan, nullptr, 0));
    vriEnumerateAdapters(VriGraphicsAPI_Vulkan, adapters.data(), uint32_t(adapters.size()));
    bool supportsRayQuery = false;
    for (uint32_t index = 0; index < adapters.size(); ++index)
    {
        auto desc                                                            = vriDefaultDeviceCreationDesc();
        desc.graphicsAPI                                                     = VriGraphicsAPI_Vulkan;
        desc.adapterIndex                                                    = index;
        desc.enabledFeatures                                                 = required;
        desc.bestEffort                                                      = VRI_TRUE;
        desc.enableValidation                                                = VRI_TRUE;
        VriDevice*                                                    handle = nullptr;
        const auto                                                    result = vriCreateDevice(&desc, &handle);
        const std::unique_ptr<VriDevice, decltype(&vriDestroyDevice)> probe(handle, vriDestroyDevice);
        require(result == VriResult_Success, "Could not probe a GPU for the Ray Query regression");
        VriCoreInterface core {};
        must(vultra::check(vriGetInterface(probe.get(), VRI_INTERFACE_CORE, sizeof(core), &core),
                           "Get probe interface"));
        supportsRayQuery |= (core.GetDeviceDesc(probe.get())->enabledFeatures & required) == required;
    }
    if (supportsRayQuery)
    {
        const auto  device = must(vultra::Device::create(true, nullptr, required));
        const auto& info   = *device->core.GetDeviceDesc(device->handle);
        require(info.hasRayQuery && info.hasBindless && (info.enabledFeatures & required) == required,
                "GPU selection silently dropped required Ray Query or bindless support");
        const auto frame = must(vultra::Frame::create(*device));
        must(frame->begin());
        must(frame->submitAndWait());
    }
    else
    {
        std::cout << "Ray Query hardware regression skipped: no compatible adapter\n";
    }

    support::rejects(vultra::Device::create(true, nullptr, uint64_t(1) << 63),
                     "GPU selection accepted an unsupported required feature",
                     "No Vulkan GPU provides required features");
    std::cout << "GPU selection regression passed\n";
    return 0;
}
