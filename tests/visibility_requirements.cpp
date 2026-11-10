#include <vultra/drivers/rhi/device.hpp>
#include <vultra/servers/rendering/builtin/visibility_requirements.hpp>

#include <iostream>
#include <limits>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc == 2 &&
        (std::string_view(argv[1]) == "--probe-device" || std::string_view(argv[1]) == "--probe-visibility"))
    {
        const auto features =
            VriFeature_Bindless | (std::string_view(argv[1]) == "--probe-device" ? VriFeature_RayQuery : 0);
        auto device = vultra::Device::create(true, nullptr, features);
        if (!device)
        {
            std::cerr << "PVW feature device creation failed: " << device.error() << '\n';
            return 1;
        }
        const auto& caps      = *(*device)->core.GetDeviceDesc((*device)->handle);
        const auto  supported = vultra::validateVisibilityRequirements(
            caps,
            (*device)->core.GetFormatSupport((*device)->handle, VriFormat_RG32_UINT),
            71);
        if (!supported)
        {
            std::cerr << "Visibility requirements: " << supported.error() << '\n';
            return 1;
        }
        std::cout << "PVW device features=" << caps.enabledFeatures << " bindless=" << caps.hasBindless
                  << " rayQuery=" << caps.hasRayQuery << " textureCapacity=" << caps.bindlessTextureMaxNum
                  << " samplerCapacity=" << caps.bindlessSamplerMaxNum << '\n';
        return 0;
    }
    VriDeviceDesc caps {};
    caps.hasBindless                       = VRI_TRUE;
    caps.bindlessTextureMaxNum             = 510;
    caps.bindlessSamplerMaxNum             = 498;
    constexpr VriFormatSupportFlags format = VriFormatSupport_Texture | VriFormatSupport_ColorAttachment;
    if (!vultra::validateVisibilityRequirements(caps, format, 71))
        return 1;
    if (vultra::validateVisibilityRequirements(caps, format, 0) ||
        vultra::validateVisibilityRequirements(caps, format, size_t(std::numeric_limits<uint32_t>::max())))
        return 2;
    if (vultra::validateVisibilityRequirements(caps, VriFormatSupport_Texture, 71))
        return 3;
    caps.bindlessSamplerMaxNum = 497;
    if (vultra::validateVisibilityRequirements(caps, format, 71))
        return 4;
    caps.bindlessSamplerMaxNum = 498;
    caps.bindlessTextureMaxNum = 509;
    if (vultra::validateVisibilityRequirements(caps, format, 71))
        return 5;
    caps.bindlessTextureMaxNum = 20;
    caps.bindlessSamplerMaxNum = 8;
    if (vultra::validateVisibilityRequirements(caps, format, 71) ||
        !vultra::validateVisibilityBatchedRequirements(caps, format, 71) ||
        !vultra::validateVisibilityFragmentRequirements(caps, format, 71))
        return 6;
    caps.bindlessSamplerMaxNum = 7;
    if (vultra::validateVisibilityFragmentRequirements(caps, format, 71))
        return 7;
    caps.bindlessTextureMaxNum = 510;
    caps.bindlessSamplerMaxNum = 498;
    caps.hasBindless           = VRI_FALSE;
    if (vultra::validateVisibilityRequirements(caps, format, 71))
        return 8;
    return 0;
}
