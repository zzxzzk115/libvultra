#pragma once

#include <glm/vec3.hpp>
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
        // Appended at the end (offset 80) so existing byte offsets stay stable for
        // the GLSL loader in gpu_scene.glsl. rgb = emissive color x strength; a unused.
        glm::vec4 emissiveFactor {0, 0, 0, 1};
    };
    static_assert(sizeof(MaterialParamsPBRMR) % 16 == 0);

    // PBR specular-glossiness. The GBuffer is metallic-roughness shaped, so the
    // shader (thin_gbuffer material_mra) collapses specular/glossiness to mra at
    // write time and the deferred lighting reads mra.x as F0 for this model.
    struct alignas(16) MaterialParamsPBRSG
    {
        glm::vec4 diffuseColor {1, 1, 1, 1};
        glm::vec3 specularFactor {1, 1, 1};
        float     glossinessFactor {1.0f};
        uint32_t  diffuseColorTex {0};
        uint32_t  specularGlossinessTex {0};
        uint32_t  glossinessTex {0};
        uint32_t  normalTex {0};
        // Appended at offset 48 (GLSL loader reads it there). rgb = emissive; a unused.
        glm::vec4 emissiveFactor {0, 0, 0, 1};
    };
    static_assert(sizeof(MaterialParamsPBRSG) % 16 == 0);

    struct alignas(16) MaterialParamsUnlit
    {
        glm::vec4 color {1, 1, 1, 1};
        uint32_t  colorTex {0};
        uint32_t  pad0 {0};
        uint32_t  pad1 {0};
        uint32_t  pad2 {0};
    };
    static_assert(sizeof(MaterialParamsUnlit) % 16 == 0);

    struct alignas(16) MaterialParamsPhong
    {
        glm::vec4 diffuse {1, 1, 1, 1};
        glm::vec4 specularShininess {1, 1, 1, 32}; // xyz = specular, w = shininess
        uint32_t  diffuseTex {0};
        uint32_t  pad0 {0};
        uint32_t  pad1 {0};
        uint32_t  pad2 {0};
        // Appended at offset 48 (GLSL loader reads it there). rgb = emissive; a unused.
        glm::vec4 emissiveFactor {0, 0, 0, 1};
    };
    static_assert(sizeof(MaterialParamsPhong) % 16 == 0);

    // Toon / cel shading. Lighting runs Cook-Torrance then quantizes, so it reads
    // the metallic-roughness GBuffer like PBR-MR; this block only needs the authored
    // surface color, emissive, and ambient occlusion (metallic/roughness are fixed).
    struct alignas(16) MaterialParamsToon
    {
        glm::vec4 baseColor {1, 1, 1, 1};
        glm::vec4 emissiveAo {0, 0, 0, 1}; // rgb = emissive color x strength, a = ambient occlusion
        uint32_t  baseColorTex {0};
        uint32_t  pad0 {0};
        uint32_t  pad1 {0};
        uint32_t  pad2 {0};
    };
    static_assert(sizeof(MaterialParamsToon) % 16 == 0);
} // namespace vultra
