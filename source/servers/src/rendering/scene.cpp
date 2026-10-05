#include "upload.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        struct PrimitiveTransform
        {
            glm::mat4 model;
            glm::mat4 normal;
            glm::vec4 properties; // Tangent handedness, conservative sphere scale.
        };

        static_assert(sizeof(PrimitiveTransform) == 144 && offsetof(PrimitiveTransform, properties) == 128);

        PrimitiveTransform transformData(const glm::mat4& model)
        {
            if (model[0][3] != 0 || model[1][3] != 0 || model[2][3] != 0 || model[3][3] != 1)
            {
                throw std::invalid_argument("Primitive transform must be affine");
            }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(model[column][row]))
                    {
                        throw std::invalid_argument("Primitive transform must be finite");
                    }
                }
            }
            const glm::mat3 linear(model);
            const float     determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0)
            {
                throw std::invalid_argument("Primitive transform must be invertible");
            }
            float columnNorm = 0;
            float rowNorm    = 0;
            for (int index = 0; index < 3; ++index)
            {
                float columnSum = 0;
                float rowSum    = 0;
                for (int other = 0; other < 3; ++other)
                {
                    columnSum += std::abs(linear[index][other]);
                    rowSum += std::abs(linear[other][index]);
                }
                columnNorm = std::max(columnNorm, columnSum);
                rowNorm    = std::max(rowNorm, rowSum);
            }
            const float sphereScale = std::sqrt(columnNorm * rowNorm);
            if (!std::isfinite(sphereScale))
            {
                throw std::invalid_argument("Primitive transform exceeds finite bounds");
            }
            return {model,
                    glm::mat4(glm::transpose(glm::inverse(linear))),
                    {determinant < 0 ? -1.0f : 1.0f, sphereScale, 0, 0}};
        }

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
        radius(scene.radius),
        m_Device(device)
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
        transforms =
            std::make_unique<Buffer>(device,
                                     VriBufferDesc {std::max<size_t>(primitives.size(), 1) * sizeof(PrimitiveTransform),
                                                    sizeof(PrimitiveTransform),
                                                    VriBufferUsage_StorageBuffer,
                                                    VriMemoryLocation_HostUpload});
        m_Mirrored.resize(primitives.size());
        m_Transforms.resize(primitives.size());
        setPrimitiveTransforms(0, uint32_t(primitives.size()), glm::mat4(1));
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
        const std::array<SceneVertex, 1> emptyVertices {};
        const std::array<uint32_t, 1>    emptyIndices {};
        if (vertexData.empty())
        {
            vertexData = emptyVertices;
        }
        std::span<const uint32_t> indexData = scene.indices;
        if (indexData.empty())
        {
            indexData = emptyIndices;
        }
        const auto rayFeatures =
            device.core.GetDeviceDesc(device.handle)->enabledFeatures & (VriFeature_RayQuery | VriFeature_RayTracing);
        const VriBufferUsageFlags accelerationUsage = rayFeatures ? VriBufferUsage_AccelerationBuildInput : 0u;
        const VriBufferUsageFlags vertexUsage =
            VriBufferUsage_VertexBuffer | VriBufferUsage_StorageBuffer | accelerationUsage;
        const VriAccessStage vertexReady {
            VriAccess_VertexBufferRead | (meshShading ? VriAccess_ShaderResourceRead : 0ull),
            VriPipelineStage_VertexInput | (meshShading ? VriPipelineStage_MeshShader : 0ull)};
        vertices = uploadBuffer(device, std::as_bytes(vertexData), vertexUsage, vertexReady);
        indices  = uploadBuffer(device,
                                std::as_bytes(indexData),
                                VriBufferUsage_IndexBuffer | VriBufferUsage_StorageBuffer | accelerationUsage,
                                {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput},
                                sizeof(uint32_t));
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

    void GpuScene::setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform)
    {
        if (first > primitives.size() || count > primitives.size() - first)
        {
            throw std::out_of_range("Primitive transform range");
        }
        const auto data = transformData(transform);
        if (count == 0)
        {
            return;
        }
        if (m_TransformRevision == UINT64_MAX)
        {
            throw std::overflow_error("Primitive transform revision exhausted");
        }
        const uint64_t offset = uint64_t(first) * sizeof(PrimitiveTransform);
        auto*          mapped = static_cast<std::byte*>(
            m_Device.core.MapBuffer(transforms->handle, offset, uint64_t(count) * sizeof(PrimitiveTransform)));
        if (!mapped)
        {
            throw std::runtime_error("Map primitive transforms");
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            std::memcpy(mapped + uint64_t(index) * sizeof(data), &data, sizeof(data));
            m_Mirrored[first + index]   = data.properties.x < 0;
            m_Transforms[first + index] = transform;
        }
        m_Device.core.UnmapBuffer(transforms->handle);
        ++m_TransformRevision;
    }

    bool GpuScene::primitiveMirrored(uint32_t index) const
    {
        return m_Mirrored.at(index) != 0;
    }

    const glm::mat4& GpuScene::primitiveTransform(uint32_t index) const
    {
        return m_Transforms.at(index);
    }

    uint64_t GpuScene::transformRevision() const
    {
        return m_TransformRevision;
    }
} // namespace vultra
