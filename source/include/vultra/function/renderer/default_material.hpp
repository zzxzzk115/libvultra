#pragma once

#include "vultra/core/base/string_util.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        struct PBRMaterial
        {
            std::string name;

            Ref<rhi::Texture> albedo {nullptr};
            glm::vec4         baseColor {1.0f, 1.0f, 1.0f, 1.0f};
            bool              useAlbedoTexture {false};

            Ref<rhi::Texture> alphaMask {nullptr};
            bool              useAlphaMaskTexture {false};

            float           opacity {1.0f};
            rhi::BlendState blendState {.enabled = false};

            Ref<rhi::Texture> metallic {nullptr};
            float             metallicFactor {0.0f};
            bool              useMetallicTexture {false};

            Ref<rhi::Texture> roughness {nullptr};
            float             roughnessFactor {0.0f};
            bool              useRoughnessTexture {false};

            Ref<rhi::Texture> specular {nullptr};
            bool              useSpecularTexture {false};

            Ref<rhi::Texture> normal {nullptr};
            bool              useNormalTexture {false};

            Ref<rhi::Texture> ao {nullptr};
            bool              useAOTexture {false};

            Ref<rhi::Texture> emissive {nullptr};
            glm::vec4         emissiveColorIntensity {0.0f, 0.0f, 0.0f, 1.0f};
            bool              useEmissiveTexture {false};

            float ior {1.0f};

            // GLTF
            Ref<rhi::Texture> metallicRoughness {nullptr};
            bool              useMetallicRoughnessTexture {false};
            bool              doubleSided {false};

            bool isDecal() const
            {
                auto upperCaseName = util::toupper_str(name);
                return blendState.enabled && upperCaseName.find("DECAL") != std::string::npos;
            }
        };
    } // namespace gfx
} // namespace vultra