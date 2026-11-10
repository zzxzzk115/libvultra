#pragma once

#include <vri/vri.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>

namespace vultra
{
    // Material-table and visibility-target checks adapted from the PVW companion. Resolve-pipeline
    // creation also checks buffer paging and stage-specific capabilities.
    inline std::expected<void, std::string> validateVisibilityRequirements(const VriDeviceDesc&  caps,
                                                                           VriFormatSupportFlags visibilityFormat,
                                                                           size_t                materialCount)
    {
        constexpr size_t slotsPerMaterial = 7;
        if (!caps.hasBindless)
            return std::unexpected("Visibility rendering requires enabled bindless resources");
        constexpr uint32_t otherTextures = 13;
        constexpr uint32_t otherSamplers = 1;
        if (materialCount == 0 ||
            materialCount > (std::numeric_limits<uint32_t>::max() - otherTextures) / slotsPerMaterial)
            return std::unexpected("Visibility material count is empty or exceeds descriptor limits");
        const auto slots = uint32_t(materialCount * slotsPerMaterial);
        if (slots + otherTextures > caps.bindlessTextureMaxNum || slots + otherSamplers > caps.bindlessSamplerMaxNum)
            return std::unexpected("Visibility rendering requires more bindless texture or sampler descriptors");
        constexpr VriFormatSupportFlags required = VriFormatSupport_Texture | VriFormatSupport_ColorAttachment;
        if ((visibilityFormat & required) != required)
            return std::unexpected("Visibility rendering requires sampleable RG32_UINT color attachments");
        return {};
    }

    // The fragment-only fallback binds one material per draw and needs seven texture/sampler slots.
    inline std::expected<void, std::string>
    validateVisibilityBatchedRequirements(const VriDeviceDesc&  caps,
                                          VriFormatSupportFlags visibilityFormat,
                                          size_t                materialCount)
    {
        if (materialCount == 0 || materialCount > (std::numeric_limits<uint32_t>::max() - 13u) / 7u)
            return std::unexpected("Visibility material count is empty or exceeds descriptor limits");
        return validateVisibilityRequirements(caps, visibilityFormat, 1);
    }

    inline std::expected<void, std::string>
    validateVisibilityFragmentRequirements(const VriDeviceDesc&  caps,
                                           VriFormatSupportFlags visibilityFormat,
                                           size_t                materialCount)
    {
        if (auto direct = validateVisibilityRequirements(caps, visibilityFormat, materialCount); direct)
            return {};
        return validateVisibilityBatchedRequirements(caps, visibilityFormat, materialCount);
    }
} // namespace vultra
