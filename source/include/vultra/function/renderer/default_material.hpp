#pragma once

#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        struct PBRMaterial
        {
            Ref<rhi::Texture> albedo {nullptr};
            glm::vec4         baseColor {1.0f, 1.0f, 1.0f, 1.0f};
            bool              useAlbedoTexture {false};

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
            bool              useEmissiveTexture {false};

            // GLTF
            Ref<rhi::Texture> metallicRoughness {nullptr};
            bool              useMetallicRoughnessTexture {false};
            bool              doubleSided {false};
        };
    } // namespace gfx
} // namespace vultra