#include "upload.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <algorithm>

namespace vultra
{
    namespace
    {
        VriSamplerDesc gpuSampler(const SceneSampler& sampler)
        {
            VriSamplerDesc result {};
            auto           address = [](SamplerAddressMode mode)
            {
                switch (mode)
                {
                    case SamplerAddressMode::eRepeat:
                        return VriAddressMode_Repeat;
                    case SamplerAddressMode::eClampToEdge:
                        return VriAddressMode_ClampToEdge;
                    case SamplerAddressMode::eMirroredRepeat:
                        return VriAddressMode_MirroredRepeat;
                }
                throw std::invalid_argument("Invalid asset sampler address mode");
            };
            result.addressModeU = address(sampler.addressModeU);
            result.addressModeV = address(sampler.addressModeV);
            result.addressModeW = address(sampler.addressModeW);
            auto filter         = [](SamplerFilter value)
            {
                switch (value)
                {
                    case SamplerFilter::eNearest:
                        return VriFilter_Nearest;
                    case SamplerFilter::eLinear:
                        return VriFilter_Linear;
                }
                throw std::invalid_argument("Invalid asset sampler filter");
            };
            auto mipmap = [](SamplerMipmapMode value)
            {
                switch (value)
                {
                    case SamplerMipmapMode::eNearest:
                        return VriMipmapMode_Nearest;
                    case SamplerMipmapMode::eLinear:
                        return VriMipmapMode_Linear;
                }
                throw std::invalid_argument("Invalid asset sampler mipmap mode");
            };
            result.minFilter  = filter(sampler.minFilter);
            result.magFilter  = filter(sampler.magFilter);
            result.mipmapMode = mipmap(sampler.mipmapMode);
            result.maxLod     = sampler.maxLod;
            return result;
        }
    } // namespace

    GpuScene::GpuScene(Device& device, const SceneData& scene, bool meshShading, uint32_t workers) :
        GpuScene(device,
                 scene,
                 prepareTextures(scene, {.compression = TextureCompression::eNone}),
                 meshShading,
                 workers)
    {
    }

    GpuScene::GpuScene(Device& device, const ImportedAsset& asset, bool meshShading, uint32_t workers) :
        GpuScene(device, asset.scene, asset.textures, meshShading, workers)
    {
    }

    GpuScene::GpuScene(Device&                 device,
                       const SceneData&        scene,
                       const PreparedTextures& prepared,
                       bool                    meshShading,
                       uint32_t                workers) :
        materials(scene.materials),
        primitives(scene.primitives),
        center(scene.center),
        radius(scene.radius)
    {
        samplers.reserve(scene.samplers.size());
        for (const auto& sampler : scene.samplers)
        {
            samplers.push_back(gpuSampler(sampler));
        }
        if (materials.empty())
        {
            throw std::invalid_argument("SceneData needs at least one material");
        }
        for (const auto& primitive : primitives)
        {
            if (primitive.material >= materials.size() ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices.size())
            {
                throw std::invalid_argument("Invalid scene primitive");
            }
        }
        for (uint32_t index : scene.indices)
        {
            if (index >= scene.vertices.size())
            {
                throw std::invalid_argument("Invalid scene vertex index");
            }
        }
        std::vector<SceneVertex>     generated;
        std::span<const SceneVertex> vertexData = scene.vertices;

        const bool normalMapped = std::ranges::any_of(materials,
                                                      [](const auto& material)
                                                      {
                                                          return material.normalTexture.image >= 0;
                                                      });
        if (normalMapped && std::ranges::any_of(scene.vertices,
                                                [](const auto& vertex)
                                                {
                                                    return glm::dot(glm::vec3(vertex.tangent),
                                                                    glm::vec3(vertex.tangent)) == 0;
                                                }))
        {
            generated = scene.vertices;
            generateTangents(generated, scene.indices);
            vertexData = generated;
        }
        const VriBufferUsageFlags vertexUsage =
            VriBufferUsage_VertexBuffer | (meshShading ? VriBufferUsage_StorageBuffer : 0u);
        const VriAccessStage vertexReady {
            VriAccess_VertexBufferRead | (meshShading ? VriAccess_ShaderResourceRead : 0ull),
            VriPipelineStage_VertexInput | (meshShading ? VriPipelineStage_MeshShader : 0ull)};
        vertices = uploadBuffer(device, std::as_bytes(vertexData), vertexUsage, vertexReady);
        indices  = uploadBuffer(device,
                                std::as_bytes(std::span(scene.indices)),
                                VriBufferUsage_IndexBuffer,
                                {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        if (meshShading)
        {
            meshlets = std::make_unique<GpuMeshlets>(device, *vertices, scene, workers);
        }
        if (prepared.materials.size() != materials.size())
        {
            throw std::invalid_argument("Material texture table size mismatch");
        }
        for (const auto& image : prepared.images)
        {
            textures.push_back(
                uploadTexture(device, image.format, asset_detail::textureLayout(image.format).bytes, image.levels));
        }
        for (const auto& indices : prepared.materials)
        {
            std::array<Texture*, kMaterialTextureCount> slots {};
            for (size_t slot = 0; slot < slots.size(); ++slot)
            {
                slots[slot] = textures.at(indices[slot]).get();
            }
            materialTextures.push_back(slots);
        }
    }
} // namespace vultra
