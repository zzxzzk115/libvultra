#pragma once

#include <vultra/core/rhi/resources.hpp>
#include <vultra/function/asset/source_file.hpp>
#include <vultra/function/renderer/meshlets.hpp>

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <memory>
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

    // Opaque real-time OpenPBR parameter subset. No transmission, subsurface, fuzz or thin film.
    struct SurfaceMaterial
    {
        glm::vec4 baseColor {1};
        float     baseWeight           = 1;
        float     baseMetalness        = 0;
        float     baseDiffuseRoughness = 0;
        float     specularWeight       = 1;
        glm::vec3 specularColor {1};
        float     specularRoughness = 0.3f;
        float     specularIor       = 1.5f;
        float     coatWeight        = 0;
        float     coatRoughness     = 0.1f;
        float     coatIor           = 1.5f;
        glm::vec3 emissionColor {0};
        float     emissionLuminance      = 1;
        float     normalScale            = 1;
        float     occlusionStrength      = 1;
        float     alphaCutoff            = -1;
        int       baseColorImage         = -1;
        int       metallicRoughnessImage = -1;
        int       normalImage            = -1;
        int       occlusionImage         = -1;
        int       emissionImage          = -1;
        int       specularImage          = -1;
        int       specularColorImage     = -1;
    };

    struct ScenePrimitive
    {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t material;
    };

    // Plain static data: loaders and procedural experiments fill the same arrays.
    struct Scene
    {
        std::vector<SceneVertex>     vertices;
        std::vector<uint32_t>        indices;
        std::vector<SceneImage>      images;
        std::vector<SurfaceMaterial> materials;
        std::vector<ScenePrimitive>  primitives;
        glm::vec3                    center {0};
        float                        radius = 1;
    };

    // Accumulate UV derivatives in triangle order, then orthogonalize against the vertex normal.
    // Nonzero authored tangents are preserved. Indices may reference a range starting at vertexOffset.
    void
    generateTangents(std::span<SceneVertex> vertices, std::span<const uint32_t> indices, uint32_t vertexOffset = 0);

    Scene loadGltf(const std::filesystem::path& path, const SourceObserver& observer = {}, uint32_t workers = 0);
    // Static OBJ geometry and untextured MTL diffuse/emission; used by the original Cornell Box.
    Scene loadObj(const std::filesystem::path& path, const SourceObserver& observer = {}, uint32_t workers = 0);

    Scene      loadFbx(const std::filesystem::path& path, const SourceObserver& observer = {}, uint32_t workers = 0);
    SceneImage loadSceneImage(const std::filesystem::path& path, const SourceObserver& observer = {});

    struct ImportedAsset;
    struct PreparedTextures;

    class GpuScene
    {
    public:
        GpuScene(Device& device, const Scene& scene, bool meshShading = false, uint32_t workers = 0);
        GpuScene(Device& device, const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
        // Seven independently typed material slots; color slots use sRGB views.
        std::unique_ptr<Buffer>                                  vertices;
        std::unique_ptr<Buffer>                                  indices;
        std::vector<std::unique_ptr<Texture>>                    textures;
        std::vector<std::array<Texture*, kMaterialTextureCount>> materialTextures;
        std::vector<SurfaceMaterial>                             materials;
        std::vector<ScenePrimitive>                              primitives;
        glm::vec3                                                center;
        float                                                    radius;
        std::unique_ptr<GpuMeshlets>                             meshlets;

    private:
        GpuScene(Device&                 device,
                 const Scene&            scene,
                 const PreparedTextures& prepared,
                 bool                    meshShading,
                 uint32_t                workers);
    };
} // namespace vultra
