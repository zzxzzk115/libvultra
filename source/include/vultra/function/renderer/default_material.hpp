#pragma once

#include "vultra/core/base/string_util.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        struct PBRMaterial
        {
            std::string name;

            uint32_t  albedoIndex {0};
            glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};

            uint32_t alphaMaskIndex {0};

            float           opacity {1.0f};
            rhi::BlendState blendState {.enabled = false};

            uint32_t metallicIndex {0};
            float    metallicFactor {0.0f};

            uint32_t roughnessIndex {0};
            float    roughnessFactor {0.0f};

            uint32_t specularIndex {0};

            uint32_t normalIndex {0};

            uint32_t aoIndex {0};

            uint32_t  emissiveIndex {0};
            glm::vec4 emissiveColorIntensity {0.0f, 0.0f, 0.0f, 1.0f};

            float ior {1.0f};

            // GLTF
            uint32_t metallicRoughnessIndex {0};
            bool     doubleSided {false};

            bool isDecal() const
            {
                auto upperCaseName = util::toupper_str(name);
                return blendState.enabled && upperCaseName.find("DECAL") != std::string::npos;
            }
        };
    } // namespace gfx
} // namespace vultra