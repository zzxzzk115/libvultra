#include <vultra/drivers/rhi/device.hpp>

#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    {
        vultra::Device device;
        require(device.queue != nullptr, "Automatic GPU selection did not provide a graphics queue");
        vultra::Frame frame(device);
        frame.begin();
        frame.submitAndWait();
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
        vultra::check(vriGetInterface(probe.get(), VRI_INTERFACE_CORE, sizeof(core), &core), "Get probe interface");
        supportsRayQuery |= (core.GetDeviceDesc(probe.get())->enabledFeatures & required) == required;
    }
    if (supportsRayQuery)
    {
        vultra::Device device(true, nullptr, required);
        const auto&    info = *device.core.GetDeviceDesc(device.handle);
        require(info.hasRayQuery && info.hasBindless && (info.enabledFeatures & required) == required,
                "GPU selection silently dropped required Ray Query or bindless support");
        vultra::Frame frame(device);
        frame.begin();
        frame.submitAndWait();
    }
    else
    {
        std::cout << "Ray Query hardware regression skipped: no compatible adapter\n";
    }

    bool rejected = false;
    try
    {
        const vultra::Device unsupported(true, nullptr, uint64_t(1) << 63);
    }
    catch (const std::runtime_error& error)
    {
        rejected =
            std::string_view(error.what()).find("No Vulkan GPU provides required features") != std::string_view::npos;
    }
    require(rejected, "GPU selection accepted an unsupported required feature");
    std::cout << "GPU selection regression passed\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
