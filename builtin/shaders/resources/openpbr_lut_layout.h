#pragma once

#ifdef __cplusplus
#include <glm/glm.hpp>
#include <openpbr_data_constants.h>

namespace vultra::detail
{
    using OpenPbrLutRegion = glm::uvec4;
#define VULTRA_OPENPBR_LAYOUT_CONSTANT inline constexpr
#else
#include <openpbr/openpbr_data_constants.h>

typedef uint4 OpenPbrLutRegion;
#define VULTRA_OPENPBR_LAYOUT_CONSTANT static const
#endif

    VULTRA_OPENPBR_LAYOUT_CONSTANT int openPbrLutWidth  = OpenPBR_EnergyTableSize;
    VULTRA_OPENPBR_LAYOUT_CONSTANT int openPbrLutPlane  = OpenPBR_EnergyTableSize * OpenPBR_EnergyTableSize;
    VULTRA_OPENPBR_LAYOUT_CONSTANT int openPbrLutVolume = openPbrLutPlane * OpenPBR_EnergyTableSize;

    // Texel offset, width, height and depth in the FP32 atlas. Row zero retains the same metadata for inspection.
    // Sharing this immutable layout lets shaders calculate data addresses without a dependent metadata fetch.
    VULTRA_OPENPBR_LAYOUT_CONSTANT OpenPbrLutRegion openPbrLutRegions[8] = {
        OpenPbrLutRegion(openPbrLutWidth, openPbrLutWidth, OpenPBR_EnergyTableSize, OpenPBR_EnergyTableSize),
        OpenPbrLutRegion(openPbrLutWidth + openPbrLutVolume, openPbrLutWidth, OpenPBR_EnergyTableSize, 1),
        OpenPbrLutRegion(openPbrLutWidth + openPbrLutVolume + openPbrLutPlane,
                         openPbrLutWidth,
                         OpenPBR_EnergyTableSize,
                         1),
        OpenPbrLutRegion(openPbrLutWidth + openPbrLutVolume + 2 * openPbrLutPlane,
                         openPbrLutWidth,
                         OpenPBR_EnergyTableSize,
                         OpenPBR_EnergyTableSize),
        OpenPbrLutRegion(openPbrLutWidth + 2 * openPbrLutVolume + 2 * openPbrLutPlane,
                         openPbrLutWidth,
                         OpenPBR_EnergyTableSize,
                         1),
        OpenPbrLutRegion(openPbrLutWidth + 2 * openPbrLutVolume + 3 * openPbrLutPlane,
                         openPbrLutWidth,
                         OpenPBR_EnergyTableSize,
                         1),
        OpenPbrLutRegion(openPbrLutWidth + 2 * openPbrLutVolume + 4 * openPbrLutPlane, openPbrLutWidth, 1, 1),
        OpenPbrLutRegion(2 * openPbrLutWidth + 2 * openPbrLutVolume + 4 * openPbrLutPlane,
                         OpenPBR_LTCTableSize,
                         OpenPBR_LTCTableSize,
                         1)};
    VULTRA_OPENPBR_LAYOUT_CONSTANT int openPbrLutTexels =
        2 * openPbrLutWidth + 2 * openPbrLutVolume + 4 * openPbrLutPlane + OpenPBR_LTCTableSize * OpenPBR_LTCTableSize;

#undef VULTRA_OPENPBR_LAYOUT_CONSTANT
#ifdef __cplusplus
} // namespace vultra::detail
#endif
