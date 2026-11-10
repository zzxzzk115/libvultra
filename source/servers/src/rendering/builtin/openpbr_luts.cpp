#include "resources/openpbr_lut_layout.h"

#include <vultra/servers/rendering/builtin/openpbr_luts.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <glm/glm.hpp>
#include <interop/openpbr_interop.h>

// Only the LUT arrays are needed on the renderer's CPU side, not the BSDF functions.
#define OPENPBR_USE_TEXTURE_LUTS 0
#include <impl/data/openpbr_energy_arrays.h>
#include <impl/data/openpbr_ltc_array.h>

#include <cstring>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr bool matchesLutRegion(size_t count, int id)
        {
            const auto& region = detail::openPbrLutRegions[id];
            return count == size_t(region.y) * region.z * region.w;
        }

        static_assert(OpenPBR_LutId_LTC + 1 == std::size(detail::openPbrLutRegions));
        static_assert(OpenPBR_LutId_LTC < detail::openPbrLutWidth);
        static_assert(detail::openPbrLutTexels % detail::openPbrLutWidth == 0);
        static_assert(detail::openPbrLutTexels < (1 << 24));
        static_assert(matchesLutRegion(std::size(OpenPBR_IdealDielectricEnergyComplement_Array),
                                       OpenPBR_LutId_IdealDielectricEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_IdealDielectricAverageEnergyComplement_Array),
                                       OpenPBR_LutId_IdealDielectricAverageEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_IdealDielectricReflectionRatio_Array),
                                       OpenPBR_LutId_IdealDielectricReflectionRatio));
        static_assert(matchesLutRegion(std::size(OpenPBR_OpaqueDielectricEnergyComplement_Array),
                                       OpenPBR_LutId_OpaqueDielectricEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_OpaqueDielectricAverageEnergyComplement_Array),
                                       OpenPBR_LutId_OpaqueDielectricAverageEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_IdealMetalEnergyComplement_Array),
                                       OpenPBR_LutId_IdealMetalEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_IdealMetalAverageEnergyComplement_Array),
                                       OpenPBR_LutId_IdealMetalAverageEnergyComplement));
        static_assert(matchesLutRegion(std::size(OpenPBR_LTC_Array), OpenPBR_LutId_LTC));

        TextureData openPbrLutData()
        {
            // One 32-wide atlas. Row 0 stores {offset, width, height, depth} for each upstream LUT ID.
            // Tables follow in upstream row-major order, with normalized energy in R and LTC in RGB.
            std::vector<glm::vec4> pixels(detail::openPbrLutTexels, glm::vec4(0));
            auto                   appendEnergy = [&](int id, std::span<const OpenPBR_EnergyTableElement> table)
            {
                const auto& region = detail::openPbrLutRegions[id];
                pixels[id]         = glm::vec4(region);
                for (size_t i = 0; i < table.size(); ++i)
                {
                    pixels[region.x + i] = {float(table[i]) / 65535.0f, 0, 0, 0};
                }
            };
            appendEnergy(OpenPBR_LutId_IdealDielectricEnergyComplement, OpenPBR_IdealDielectricEnergyComplement_Array);
            appendEnergy(OpenPBR_LutId_IdealDielectricAverageEnergyComplement,
                         OpenPBR_IdealDielectricAverageEnergyComplement_Array);
            appendEnergy(OpenPBR_LutId_IdealDielectricReflectionRatio, OpenPBR_IdealDielectricReflectionRatio_Array);
            appendEnergy(OpenPBR_LutId_OpaqueDielectricEnergyComplement,
                         OpenPBR_OpaqueDielectricEnergyComplement_Array);
            appendEnergy(OpenPBR_LutId_OpaqueDielectricAverageEnergyComplement,
                         OpenPBR_OpaqueDielectricAverageEnergyComplement_Array);
            appendEnergy(OpenPBR_LutId_IdealMetalEnergyComplement, OpenPBR_IdealMetalEnergyComplement_Array);
            appendEnergy(OpenPBR_LutId_IdealMetalAverageEnergyComplement,
                         OpenPBR_IdealMetalAverageEnergyComplement_Array);
            const auto& ltc           = detail::openPbrLutRegions[OpenPBR_LutId_LTC];
            pixels[OpenPBR_LutId_LTC] = glm::vec4(ltc);
            for (size_t i = 0; i < std::size(OpenPBR_LTC_Array); ++i)
            {
                pixels[ltc.x + i] = {OpenPBR_LTC_Array[i], 0};
            }
            TextureLevel level;
            level.size = {detail::openPbrLutWidth, uint32_t(pixels.size() / detail::openPbrLutWidth)};
            level.bytes.resize(pixels.size() * sizeof(glm::vec4));
            std::memcpy(level.bytes.data(), pixels.data(), level.bytes.size());
            TextureData data;
            data.format = TextureFormat::eRgba32Sfloat;
            data.levels.push_back(std::move(level));
            return data;
        }
    }

    std::expected<std::unique_ptr<Texture>, std::string> createOpenPbrLutsExpected(Device& device)
    {
        auto texture = uploadTextureAssetExpected(device, openPbrLutData());
        if (!texture)
        {
            return std::unexpected("Upload OpenPBR LUT: " + texture.error());
        }
        return texture;
    }
} // namespace vultra
