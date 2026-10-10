#pragma once

#include <vultra/assets/fbx_import.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/core/math/extent.hpp>

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace vultra
{
    constexpr uint32_t kMaterialTextureCount = 7;

    struct SceneVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color {1};
        glm::vec4 tangent {0, 0, 0, 1}; // xyz direction, w handedness; zero xyz requests generation.
    };

    struct SceneImage
    {
        uint32_t             width  = 0;
        uint32_t             height = 0;
        std::vector<uint8_t> pixels;
        // DDS keeps its encoded mip chain until material color-space selection; pixels is empty.
        std::vector<std::byte> dds;
    };

    struct MaterialTexture
    {
        int image   = -1;
        int sampler = -1; // SceneData sampler index; -1 selects linear filtering, mipmaps and repeat.
    };

    // Opaque real-time OpenPBR parameter subset. No transmission, subsurface, fuzz or thin film.
    struct SurfaceMaterial
    {
        glm::vec4       baseColor {1};
        float           baseWeight           = 1;
        float           baseMetalness        = 0;
        float           baseDiffuseRoughness = 0;
        float           specularWeight       = 1;
        glm::vec3       specularColor {1};
        float           specularRoughness = 0.3f;
        float           specularIor       = 1.5f;
        float           coatWeight        = 0;
        float           coatRoughness     = 0.1f;
        float           coatIor           = 1.5f;
        glm::vec3       emissionColor {0};
        float           emissionLuminance = 1;
        float           normalScale       = 1;
        float           occlusionStrength = 1;
        float           alphaCutoff       = -1;
        bool            doubleSided       = true; // Procedural/OBJ/FBX default; glTF imports its own default (false).
        MaterialTexture baseColorTexture;
        MaterialTexture metallicRoughnessTexture;
        MaterialTexture normalTexture;
        MaterialTexture occlusionTexture;
        MaterialTexture emissionTexture;
        MaterialTexture specularTexture;
        MaterialTexture specularColorTexture;
    };

    struct ScenePrimitive
    {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t material;
    };

    // Plain static data: loaders and procedural experiments fill the same arrays.
    enum class SamplerAddressMode
    {
        eRepeat,
        eClampToEdge,
        eMirroredRepeat
    };
    enum class SamplerFilter
    {
        eNearest,
        eLinear
    };
    enum class SamplerMipmapMode
    {
        eNearest,
        eLinear
    };

    struct SceneSampler
    {
        SamplerAddressMode addressModeU = SamplerAddressMode::eRepeat;
        SamplerAddressMode addressModeV = SamplerAddressMode::eRepeat;
        SamplerAddressMode addressModeW = SamplerAddressMode::eRepeat;
        SamplerFilter      minFilter    = SamplerFilter::eLinear;
        SamplerFilter      magFilter    = SamplerFilter::eLinear;
        SamplerMipmapMode  mipmapMode   = SamplerMipmapMode::eLinear;
        float              maxLod       = 32;
    };

    struct SceneData
    {
        std::vector<SceneVertex>     vertices;
        std::vector<uint32_t>        indices;
        std::vector<SceneImage>      images;
        std::vector<SceneSampler>    samplers;
        std::vector<SurfaceMaterial> materials;
        std::vector<ScenePrimitive>  primitives;
        glm::vec3                    center {0};
        float                        radius = 1;
    };

    // Accumulate UV derivatives in triangle order, then orthogonalize against the vertex normal.
    // Nonzero authored tangents are preserved. Indices may reference a range starting at vertexOffset.
    void
    generateTangents(std::span<SceneVertex> vertices, std::span<const uint32_t> indices, uint32_t vertexOffset = 0);

    SceneData loadGltf(const std::filesystem::path& path,
                       const SourceObserver&        observer = {},
                       uint32_t                     workers  = 0,
                       const AssetSource*           source   = nullptr);
    // Static OBJ geometry and untextured MTL diffuse/emission; used by the original Cornell Box.
    SceneData loadObj(const std::filesystem::path& path,
                      const SourceObserver&        observer = {},
                      uint32_t                     workers  = 0,
                      const AssetSource*           source   = nullptr);

    SceneData  loadFbx(const std::filesystem::path& path,
                       const SourceObserver&        observer = {},
                       uint32_t                     workers  = 0,
                       const AssetSource*           source   = nullptr,
                       const FbxImportOptions&      options  = {});
    SceneImage loadSceneImage(const std::filesystem::path& path,
                              const SourceObserver&        observer = {},
                              const AssetSource*           source   = nullptr);

} // namespace vultra
