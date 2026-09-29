#include "../upload.hpp"
#include "openpbr_luts.hpp"

#include <glm/glm.hpp>
#include <interop/openpbr_interop.h>

// Only the LUT arrays are needed on the renderer's CPU side, not the BSDF functions.
#define OPENPBR_USE_TEXTURE_LUTS 0
#include <impl/data/openpbr_energy_arrays.h>
#include <impl/data/openpbr_ltc_array.h>

namespace vultra
{
    std::unique_ptr<Texture> createOpenPbrLuts(Device& device)
    {
        // One 32-wide atlas. Row 0 stores {offset, width, height, depth} for each upstream LUT ID.
        // Tables follow in upstream row-major order, with normalized energy in R and LTC in RGB.
        std::vector<glm::vec4> pixels(32, glm::vec4(0));
        auto appendEnergy = [&](int id, std::span<const OpenPBR_EnergyTableElement> table, int height, int depth)
        {
            pixels[id] = {float(pixels.size()), 32, float(height), float(depth)};
            for (auto value : table)
            {
                pixels.emplace_back(float(value) / 65535.0f, 0, 0, 0);
            }
        };
        appendEnergy(OpenPBR_LutId_IdealDielectricEnergyComplement,
                     OpenPBR_IdealDielectricEnergyComplement_Array,
                     32,
                     32);
        appendEnergy(OpenPBR_LutId_IdealDielectricAverageEnergyComplement,
                     OpenPBR_IdealDielectricAverageEnergyComplement_Array,
                     32,
                     1);
        appendEnergy(OpenPBR_LutId_IdealDielectricReflectionRatio, OpenPBR_IdealDielectricReflectionRatio_Array, 32, 1);
        appendEnergy(OpenPBR_LutId_OpaqueDielectricEnergyComplement,
                     OpenPBR_OpaqueDielectricEnergyComplement_Array,
                     32,
                     32);
        appendEnergy(OpenPBR_LutId_OpaqueDielectricAverageEnergyComplement,
                     OpenPBR_OpaqueDielectricAverageEnergyComplement_Array,
                     32,
                     1);
        appendEnergy(OpenPBR_LutId_IdealMetalEnergyComplement, OpenPBR_IdealMetalEnergyComplement_Array, 32, 1);
        appendEnergy(OpenPBR_LutId_IdealMetalAverageEnergyComplement,
                     OpenPBR_IdealMetalAverageEnergyComplement_Array,
                     1,
                     1);
        pixels[OpenPBR_LutId_LTC] = {float(pixels.size()), 32, 32, 1};
        for (const auto& value : OpenPBR_LTC_Array)
        {
            pixels.emplace_back(value, 0);
        }
        TextureLevel level;
        level.size = {32, uint32_t(pixels.size() / 32)};
        level.bytes.resize(pixels.size() * sizeof(glm::vec4));
        std::memcpy(level.bytes.data(), pixels.data(), level.bytes.size());
        return uploadTexture(device, VriFormat_RGBA32_SFLOAT, sizeof(glm::vec4), {level});
    }
} // namespace vultra
