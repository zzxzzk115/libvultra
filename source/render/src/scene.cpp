#include <vultra/assets/scene_render_data.hpp>
#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/render/scene.hpp>
#include <vultra/render/texture_upload.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

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

        // Per-primitive queries take indices from the scene's own primitive table; any other index is a bug.
        void requirePrimitive(uint32_t index, size_t count)
        {
            if (index >= count)
            {
                panic("GpuScene primitive index out of range");
            }
        }

        std::expected<PrimitiveTransform, Error> transformData(const glm::mat4& model)
        {
            if (model[0][3] != 0 || model[1][3] != 0 || model[2][3] != 0 || model[3][3] != 1)
            {
                return makeError("Primitive transform must be affine");
            }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(model[column][row]))
                    {
                        return makeError("Primitive transform must be finite");
                    }
                }
            }
            const glm::mat3 linear(model);
            const float     determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0)
            {
                return makeError("Primitive transform must be invertible");
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
                return makeError("Primitive transform exceeds finite bounds");
            }
            return PrimitiveTransform {model,
                                       glm::mat4(glm::transpose(glm::inverse(linear))),
                                       {determinant < 0 ? -1.0f : 1.0f, sphereScale, 0, 0}};
        }

        std::optional<VriAddressMode> addressMode(SamplerAddressMode mode)
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
            return std::nullopt;
        }

        std::optional<VriFilter> filterMode(SamplerFilter value)
        {
            switch (value)
            {
                case SamplerFilter::eNearest:
                    return VriFilter_Nearest;
                case SamplerFilter::eLinear:
                    return VriFilter_Linear;
            }
            return std::nullopt;
        }

        std::optional<VriMipmapMode> mipmapMode(SamplerMipmapMode value)
        {
            switch (value)
            {
                case SamplerMipmapMode::eNearest:
                    return VriMipmapMode_Nearest;
                case SamplerMipmapMode::eLinear:
                    return VriMipmapMode_Linear;
            }
            return std::nullopt;
        }

        // Sampler enums come from deserialized scene data, so out-of-range values are input errors.
        std::expected<VriSamplerDesc, Error> gpuSampler(const SceneSampler& sampler)
        {
            const auto addressU = addressMode(sampler.addressModeU);
            const auto addressV = addressMode(sampler.addressModeV);
            const auto addressW = addressMode(sampler.addressModeW);
            if (!addressU || !addressV || !addressW)
            {
                return makeError("Invalid asset sampler address mode");
            }
            const auto minFilter = filterMode(sampler.minFilter);
            const auto magFilter = filterMode(sampler.magFilter);
            if (!minFilter || !magFilter)
            {
                return makeError("Invalid asset sampler filter");
            }
            const auto mipmap = mipmapMode(sampler.mipmapMode);
            if (!mipmap)
            {
                return makeError("Invalid asset sampler mipmap mode");
            }
            VriSamplerDesc result {};
            result.addressModeU = *addressU;
            result.addressModeV = *addressV;
            result.addressModeW = *addressW;
            result.minFilter    = *minFilter;
            result.magFilter    = *magFilter;
            result.mipmapMode   = *mipmap;
            result.maxLod       = sampler.maxLod;
            return result;
        }
    } // namespace

    GpuScene::GpuScene(Device& device, const SceneData& scene) :
        materials(scene.materials),
        primitives(scene.primitives),
        center(scene.center),
        radius(scene.radius),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<GpuScene>, Error> GpuScene::create(Device&                 device,
                                                                     const SceneData&        scene,
                                                                     const PreparedTextures& prepared,
                                                                     const MeshletData*      prebuiltMeshlets)
    {
        if (prepared.materials.size() != scene.materials.size())
        {
            return makeError("Material texture table size mismatch");
        }
        for (const auto& table : prepared.materials)
        {
            for (const auto index : table)
            {
                if (index >= prepared.images.size())
                {
                    return makeError("Material texture index exceeds its prepared payload");
                }
            }
        }
        std::unique_ptr<GpuScene> result(new GpuScene(device, scene));
        result->samplers.reserve(scene.samplers.size());
        for (const auto& sampler : scene.samplers)
        {
            const auto converted = gpuSampler(sampler);
            if (!converted)
            {
                return std::unexpected(converted.error());
            }
            result->samplers.push_back(*converted);
        }
        if (scene.materials.empty())
        {
            return makeError("SceneData needs at least one material");
        }
        for (const auto& primitive : scene.primitives)
        {
            if (primitive.material >= scene.materials.size() ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices.size())
            {
                return makeError("Invalid scene primitive");
            }
        }
        for (uint32_t index : scene.indices)
        {
            if (index >= scene.vertices.size())
            {
                return makeError("Invalid scene vertex index");
            }
        }
        result->m_LocalBounds.reserve(scene.primitives.size());
        result->m_WorldBounds.resize(scene.primitives.size());
        for (const auto& primitive : scene.primitives)
        {
            PrimitiveBounds bounds;
            if (primitive.indexCount)
            {
                glm::vec3 minimum(std::numeric_limits<float>::infinity());
                glm::vec3 maximum(-std::numeric_limits<float>::infinity());
                for (uint32_t i = 0; i < primitive.indexCount; ++i)
                {
                    const auto position = scene.vertices[scene.indices[primitive.firstIndex + i]].position;
                    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                    {
                        return makeError("Scene position must be finite");
                    }
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
                bounds.center = (minimum + maximum) * .5f;
                bounds.extent = (maximum - minimum) * .5f;
            }
            result->m_LocalBounds.push_back(bounds);
        }
        auto transforms =
            Buffer::create(device,
                           VriBufferDesc {std::max<size_t>(scene.primitives.size(), 1) * sizeof(PrimitiveTransform),
                                          sizeof(PrimitiveTransform),
                                          VriBufferUsage_StorageBuffer,
                                          VriMemoryLocation_HostUpload});
        if (!transforms)
        {
            return std::unexpected(transforms.error());
        }
        result->transforms = std::make_unique<Buffer>(std::move(*transforms));
        result->m_Mirrored.resize(scene.primitives.size());
        result->m_Transforms.resize(scene.primitives.size());
        if (auto initialized = result->setPrimitiveTransforms(0, uint32_t(scene.primitives.size()), glm::mat4(1));
            !initialized)
        {
            return std::unexpected(initialized.error());
        }
        std::vector<SceneVertex>     generated;
        std::span<const SceneVertex> vertexData = scene.vertices;

        const bool normalMapped = std::ranges::any_of(scene.materials,
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
            if (auto tangents = generateTangents(generated, scene.indices); !tangents)
            {
                return std::unexpected(tangents.error());
            }
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
        const VriAccessStage vertexReady {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput};
        auto                 vertices = uploadBuffer(device, std::as_bytes(vertexData), vertexUsage, vertexReady);
        if (!vertices)
        {
            return std::unexpected(vertices.error());
        }
        result->vertices = std::make_unique<Buffer>(std::move(*vertices));
        auto indices     = uploadBuffer(device,
                                        std::as_bytes(indexData),
                                        VriBufferUsage_IndexBuffer | VriBufferUsage_StorageBuffer | accelerationUsage,
                                        {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput},
                                        sizeof(uint32_t));
        if (!indices)
        {
            return std::unexpected(indices.error());
        }
        result->indices = std::make_unique<Buffer>(std::move(*indices));
        if (prebuiltMeshlets)
        {
            if (prebuiltMeshlets->primitives.size() != scene.primitives.size())
            {
                return makeError("Meshlet primitive table does not match its scene");
            }
            auto meshlets = GpuMeshlets::create(device, *result->vertices, *prebuiltMeshlets);
            if (!meshlets)
            {
                return std::unexpected(meshlets.error());
            }
            result->meshlets = std::move(*meshlets);
        }
        for (const auto& image : prepared.images)
        {
            auto texture = uploadTextureAsset(device, image);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            result->textures.push_back(std::move(*texture));
        }
        for (const auto& table : prepared.materials)
        {
            std::array<Texture*, kMaterialTextureCount> slots {};
            for (size_t slot = 0; slot < slots.size(); ++slot)
            {
                slots[slot] = result->textures[table[slot]].get();
            }
            result->materialTextures.push_back(slots);
        }
        return result;
    }

    std::expected<void, Error>
    GpuScene::setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform)
    {
        if (first > primitives.size() || count > primitives.size() - first)
        {
            return makeError("Primitive transform range");
        }
        const auto transformed = transformData(transform);
        if (!transformed)
        {
            return std::unexpected(transformed.error());
        }
        const auto&     data = *transformed;
        const glm::mat3 absoluteLinear {glm::abs(glm::vec3(transform[0])),
                                        glm::abs(glm::vec3(transform[1])),
                                        glm::abs(glm::vec3(transform[2]))};
        if (count == 0)
        {
            return {};
        }
        if (m_TransformRevision == UINT64_MAX)
        {
            panic("Primitive transform revision exhausted");
        }
        const uint64_t offset = uint64_t(first) * sizeof(PrimitiveTransform);
        auto*          mapped = static_cast<std::byte*>(
            m_Device.core.MapBuffer(transforms->handle, offset, uint64_t(count) * sizeof(PrimitiveTransform)));
        if (!mapped)
        {
            return makeError("Map primitive transforms");
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            std::memcpy(mapped + uint64_t(index) * sizeof(data), &data, sizeof(data));
            m_Mirrored[first + index]    = data.properties.x < 0;
            m_Transforms[first + index]  = transform;
            const auto& local            = m_LocalBounds[first + index];
            m_WorldBounds[first + index] = {glm::vec3(transform * glm::vec4(local.center, 1)),
                                            absoluteLinear * local.extent};
        }
        m_Device.core.UnmapBuffer(transforms->handle);
        ++m_TransformRevision;
        return {};
    }

    bool GpuScene::primitiveMirrored(uint32_t index) const
    {
        requirePrimitive(index, primitives.size());
        return m_Mirrored[index] != 0;
    }

    const glm::mat4& GpuScene::primitiveTransform(uint32_t index) const
    {
        requirePrimitive(index, primitives.size());
        return m_Transforms[index];
    }

    uint64_t GpuScene::transformRevision() const
    {
        return m_TransformRevision;
    }

    const PrimitiveBounds& GpuScene::primitiveBounds(uint32_t index) const
    {
        requirePrimitive(index, primitives.size());
        return m_WorldBounds[index];
    }
} // namespace vultra
