#pragma once

#include <glm/vec4.hpp>

#include <cstdint>

namespace vultra
{
    // GPU-side metallic-roughness PBR material parameter block, packed for upload into the
    // material parameter buffer (std140-compatible, 16-byte aligned).
    //
    // This is the single definition shared by the render system, asset system, and the
    // DirectGBuffer pass, which previously each kept a byte-identical copy. The
    // compatibility GBuffer path intentionally uses a *different*, smaller PBR-MR layout and
    // must not use this struct.
    struct alignas(16) MaterialParamsPBRMR
    {
        glm::vec4 baseColor {1, 1, 1, 1};
        float     metallicFactor {1.0f};
        float     roughnessFactor {1.0f};
        float     alphaCutoff {0.5f};
        uint32_t  alphaMode {0};
        uint32_t  baseColorTex {0};
        uint32_t  normalTex {0};
        uint32_t  mrTex {0};
        uint32_t  metallicTex {0};
        uint32_t  roughnessTex {0};
        uint32_t  occlusionTex {0};
        uint32_t  emissiveTex {0};
        uint32_t  doubleSided {0};
        uint32_t  mrTextureMode {0};
        uint32_t  pad1 {0};
        uint32_t  pad2 {0};
    };
    static_assert(sizeof(MaterialParamsPBRMR) % 16 == 0);
} // namespace vultra
