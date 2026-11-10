#include "ray_geometry.hpp"
#include "texture_upload_detail.hpp"

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/ray_scene.hpp>
#include <vultra/servers/rendering/scene.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vultra
{
    GpuScene::~GpuScene() = default;

    std::expected<void, std::string> GpuScene::setImmutableOpaqueAlpha(std::span<const uint8_t> values)
    {
        if (!values.empty() && values.size() != materials.size())
        {
            return std::unexpected("Opaque-alpha certificate count must match the material count");
        }
        if (std::ranges::any_of(values,
                                [](uint8_t value)
                                {
                                    return value > 1;
                                }))
        {
            return std::unexpected("Opaque-alpha certificate entries must be zero or one");
        }
        m_ImmutableOpaqueAlpha.assign(values.begin(), values.end());
        return {};
    }

    bool GpuScene::hasImmutableOpaqueAlpha(uint32_t material) const
    {
        return material < m_ImmutableOpaqueAlpha.size() && m_ImmutableOpaqueAlpha[material] != 0;
    }

    namespace
    {
        struct PrimitiveTransform
        {
            glm::mat4 model;
            glm::mat4 normal;
            glm::vec4 properties; // Tangent handedness, conservative sphere scale.
        };

        static_assert(sizeof(PrimitiveTransform) == 144 && offsetof(PrimitiveTransform, properties) == 128);

        std::expected<PrimitiveTransform, std::string> transformData(const glm::mat4& model)
        {
            if (model[0][3] != 0 || model[1][3] != 0 || model[2][3] != 0 || model[3][3] != 1)
            {
                return std::unexpected("Primitive transform must be affine");
            }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(model[column][row]))
                    {
                        return std::unexpected("Primitive transform must be finite");
                    }
                }
            }
            const glm::mat3 linear(model);
            const float     determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0)
            {
                return std::unexpected("Primitive transform must be invertible");
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
                return std::unexpected("Primitive transform exceeds finite bounds");
            }
            return PrimitiveTransform {model,
                                       glm::mat4(glm::transpose(glm::inverse(linear))),
                                       {determinant < 0 ? -1.0f : 1.0f, sphereScale, 0, 0}};
        }

        std::expected<VriSamplerDesc, std::string> gpuSampler(const SceneSampler& sampler)
        {
            VriSamplerDesc result {};
            auto           address = [](SamplerAddressMode mode) -> std::expected<VriAddressMode, std::string>
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
                return std::unexpected("Invalid asset sampler address mode");
            };
            auto filter = [](SamplerFilter value) -> std::expected<VriFilter, std::string>
            {
                switch (value)
                {
                    case SamplerFilter::eNearest:
                        return VriFilter_Nearest;
                    case SamplerFilter::eLinear:
                        return VriFilter_Linear;
                }
                return std::unexpected("Invalid asset sampler filter");
            };
            auto mipmap = [](SamplerMipmapMode value) -> std::expected<VriMipmapMode, std::string>
            {
                switch (value)
                {
                    case SamplerMipmapMode::eNearest:
                        return VriMipmapMode_Nearest;
                    case SamplerMipmapMode::eLinear:
                        return VriMipmapMode_Linear;
                }
                return std::unexpected("Invalid asset sampler mipmap mode");
            };
            const auto u   = address(sampler.addressModeU);
            const auto v   = address(sampler.addressModeV);
            const auto w   = address(sampler.addressModeW);
            const auto min = filter(sampler.minFilter);
            const auto mag = filter(sampler.magFilter);
            const auto mip = mipmap(sampler.mipmapMode);
            if (!u || !v || !w)
            {
                return std::unexpected("Invalid asset sampler address mode");
            }
            if (!min || !mag)
            {
                return std::unexpected("Invalid asset sampler filter");
            }
            if (!mip)
            {
                return std::unexpected(mip.error());
            }
            result.addressModeU = *u;
            result.addressModeV = *v;
            result.addressModeW = *w;
            result.minFilter    = *min;
            result.magFilter    = *mag;
            result.mipmapMode   = *mip;
            result.maxLod       = sampler.maxLod;
            return result;
        }
    } // namespace

    GpuScene::GpuScene(Device& device, Uninitialized) :
        center(0),
        radius(0),
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<GpuScene>, std::string> GpuScene::create(Device&                 device,
                                                                           const SceneData&        scene,
                                                                           const PreparedTextures& prepared,
                                                                           const MeshletData*      prebuiltMeshlets)
    {
        auto result = std::unique_ptr<GpuScene>(new GpuScene(device, Uninitialized {}));
        if (auto initialized = result->initialize(scene, prepared, prebuiltMeshlets); !initialized)
        {
            return std::unexpected(initialized.error().message);
        }
        return result;
    }

    GpuScene::GpuScene(Device&                 device,
                       const SceneData&        scene,
                       const PreparedTextures& prepared,
                       const MeshletData*      prebuiltMeshlets) :
        GpuScene(device, Uninitialized {})
    {
        if (auto initialized = initialize(scene, prepared, prebuiltMeshlets); !initialized)
        {
            if (initialized.error().invalidInput)
            {
                throw std::invalid_argument(initialized.error().message);
            }
            throw std::runtime_error(initialized.error().message);
        }
    }

    std::expected<void, GpuScene::InitializationError>
    GpuScene::initialize(const SceneData& scene, const PreparedTextures& prepared, const MeshletData* prebuiltMeshlets)
    {
        if (prebuiltMeshlets && prebuiltMeshlets->primitives.size() != scene.primitives.size())
        {
            return std::unexpected(InitializationError {"Meshlet primitive table does not match its scene", true});
        }
        if (prebuiltMeshlets && !(m_Device.features & VriFeature_MeshShader))
        {
            return std::unexpected(
                InitializationError {"Meshlet upload requires the enabled mesh shader feature", true});
        }
        materials  = scene.materials;
        primitives = scene.primitives;
        center     = scene.center;
        radius     = scene.radius;
        if (prepared.materials.size() != materials.size())
        {
            return std::unexpected(InitializationError {"Material texture table size mismatch", true});
        }
        for (const auto& table : prepared.materials)
        {
            for (const auto index : table)
            {
                if (index >= prepared.images.size())
                {
                    return std::unexpected(
                        InitializationError {"Material texture index exceeds its prepared payload", true});
                }
            }
        }
        samplers.reserve(scene.samplers.size());
        for (const auto& sampler : scene.samplers)
        {
            auto converted = gpuSampler(sampler);
            if (!converted)
            {
                return std::unexpected(InitializationError {converted.error(), true});
            }
            samplers.push_back(*converted);
        }
        if (materials.empty())
        {
            return std::unexpected(InitializationError {"SceneData needs at least one material", true});
        }
        for (const auto& primitive : primitives)
        {
            if (primitive.material >= materials.size() ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices.size())
            {
                return std::unexpected(InitializationError {"Invalid scene primitive", true});
            }
        }
        for (uint32_t index : scene.indices)
        {
            if (index >= scene.vertices.size())
            {
                return std::unexpected(InitializationError {"Invalid scene vertex index", true});
            }
        }
        m_LocalBounds.reserve(primitives.size());
        m_WorldBounds.resize(primitives.size());
        for (const auto& primitive : primitives)
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
                        return std::unexpected(InitializationError {"Scene position must be finite", true});
                    }
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
                bounds.center = (minimum + maximum) * .5f;
                bounds.extent = (maximum - minimum) * .5f;
            }
            m_LocalBounds.push_back(bounds);
        }
        auto transformBuffer = Buffer::create(m_Device,
                                              {std::max<size_t>(primitives.size(), 1) * sizeof(PrimitiveTransform),
                                               sizeof(PrimitiveTransform),
                                               VriBufferUsage_StorageBuffer,
                                               VriMemoryLocation_HostUpload});
        if (!transformBuffer)
        {
            return std::unexpected(InitializationError {transformBuffer.error(), false});
        }
        transforms = std::move(*transformBuffer);
        m_Mirrored.resize(primitives.size());
        m_Transforms.resize(primitives.size());
        if (auto initialized = setPrimitiveTransformsExpected(0, uint32_t(primitives.size()), glm::mat4(1));
            !initialized)
        {
            return std::unexpected(InitializationError {initialized.error(), false});
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
            if (auto generatedTangents = generateTangentsExpected(generated, scene.indices); !generatedTangents)
            {
                return std::unexpected(InitializationError {generatedTangents.error(), true});
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
        const auto rayFeatures = m_Device.core.GetDeviceDesc(m_Device.handle)->enabledFeatures &
                                 (VriFeature_RayQuery | VriFeature_RayTracing);
        const VriBufferUsageFlags accelerationUsage = rayFeatures ? VriBufferUsage_AccelerationBuildInput : 0u;
        const VriBufferUsageFlags vertexUsage =
            VriBufferUsage_VertexBuffer | VriBufferUsage_StorageBuffer | accelerationUsage;
        const VriAccessStage vertexReady {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput};
        auto uploadedVertices = uploadBufferExpected(m_Device, std::as_bytes(vertexData), vertexUsage, vertexReady);
        if (!uploadedVertices)
        {
            return std::unexpected(InitializationError {uploadedVertices.error(), false});
        }
        vertices = std::move(*uploadedVertices);
        auto uploadedIndices =
            uploadBufferExpected(m_Device,
                                 std::as_bytes(indexData),
                                 VriBufferUsage_IndexBuffer | VriBufferUsage_StorageBuffer | accelerationUsage,
                                 {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput},
                                 sizeof(uint32_t));
        if (!uploadedIndices)
        {
            return std::unexpected(InitializationError {uploadedIndices.error(), false});
        }
        indices = std::move(*uploadedIndices);
        for (const auto& image : prepared.images)
        {
            auto texture = detail::uploadTextureAsset(m_Device, image);
            if (!texture)
            {
                return std::unexpected(InitializationError {texture.error().message, texture.error().invalidInput});
            }
            textures.push_back(std::move(*texture));
        }
        for (const auto& indices : prepared.materials)
        {
            std::array<Texture*, kMaterialTextureCount> slots {};
            for (size_t slot = 0; slot < slots.size(); ++slot)
            {
                slots[slot] = textures[indices[slot]].get();
            }
            materialTextures.push_back(slots);
        }
        if (prebuiltMeshlets)
        {
            auto uploaded = GpuMeshlets::create(m_Device, *vertices, *prebuiltMeshlets);
            if (!uploaded)
            {
                return std::unexpected(InitializationError {uploaded.error(), false});
            }
            meshlets = std::move(*uploaded);
        }
        return {};
    }

    void GpuScene::setPrimitiveTransforms(uint32_t first, uint32_t count, const glm::mat4& transform)
    {
        if (auto result = setPrimitiveTransformsExpected(first, count, transform); !result)
        {
            if (result.error() == "Primitive transform range")
            {
                throw std::out_of_range(result.error());
            }
            if (result.error() == "Primitive transform revision exhausted")
            {
                throw std::overflow_error(result.error());
            }
            if (result.error() == "Map primitive transforms")
            {
                throw std::runtime_error(result.error());
            }
            throw std::invalid_argument(result.error());
        }
    }

    std::expected<void, std::string>
    GpuScene::setPrimitiveTransformsExpected(uint32_t first, uint32_t count, const glm::mat4& transform)
    {
        if (first > primitives.size() || count > primitives.size() - first)
        {
            return std::unexpected("Primitive transform range");
        }
        const auto data = transformData(transform);
        if (!data)
        {
            return std::unexpected(data.error());
        }
        const glm::mat3 absoluteLinear {glm::abs(glm::vec3(transform[0])),
                                        glm::abs(glm::vec3(transform[1])),
                                        glm::abs(glm::vec3(transform[2]))};
        if (count == 0)
        {
            return {};
        }
        if (m_TransformRevision == UINT64_MAX)
        {
            return std::unexpected("Primitive transform revision exhausted");
        }
        const uint64_t offset = uint64_t(first) * sizeof(PrimitiveTransform);
        auto*          mapped = static_cast<std::byte*>(
            m_Device.core.MapBuffer(transforms->handle, offset, uint64_t(count) * sizeof(PrimitiveTransform)));
        if (!mapped)
        {
            return std::unexpected("Map primitive transforms");
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            std::memcpy(mapped + uint64_t(index) * sizeof(*data), &*data, sizeof(*data));
            m_Mirrored[first + index]    = data->properties.x < 0;
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
        return m_Mirrored.at(index) != 0;
    }

    std::expected<bool, std::string> GpuScene::primitiveMirroredExpected(uint32_t index) const
    {
        if (index >= m_Mirrored.size())
        {
            return std::unexpected("Primitive transform index is out of range");
        }
        return m_Mirrored[index] != 0;
    }

    const glm::mat4& GpuScene::primitiveTransform(uint32_t index) const
    {
        return m_Transforms.at(index);
    }

    std::expected<const glm::mat4*, std::string> GpuScene::primitiveTransformExpected(uint32_t index) const
    {
        if (index >= m_Transforms.size())
        {
            return std::unexpected("Primitive transform index is out of range");
        }
        return &m_Transforms[index];
    }

    uint64_t GpuScene::transformRevision() const
    {
        return m_TransformRevision;
    }

    std::expected<void, std::string>
    GpuScene::notifyVertexPositionsChangedExpected(std::span<const PrimitiveBounds> localBounds)
    {
        if (meshlets)
        {
            return std::unexpected("Vertex-position updates require rebuilding meshlet bounds and cones");
        }
        if (localBounds.size() != primitives.size() || localBounds.size() != m_LocalBounds.size())
        {
            return std::unexpected("Vertex-position bounds must match every existing primitive");
        }
        if (m_RayGeometry)
        {
            if (auto valid = m_RayGeometry->validateExpected(); !valid)
            {
                return valid;
            }
        }
        if (localBounds.empty())
        {
            return {};
        }
        if (m_GeometryRevision == UINT64_MAX)
        {
            return std::unexpected("Scene geometry revision exhausted");
        }
        const auto finite = [](const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        std::vector<PrimitiveBounds> worldBounds(localBounds.size());
        glm::vec3                    minimum(std::numeric_limits<float>::infinity());
        glm::vec3                    maximum(-std::numeric_limits<float>::infinity());
        for (size_t i = 0; i < localBounds.size(); ++i)
        {
            const auto& local = localBounds[i];
            if (!finite(local.center) || !finite(local.extent) || glm::any(glm::lessThan(local.extent, glm::vec3(0))))
            {
                return std::unexpected("Vertex-position bounds must be finite with nonnegative extents");
            }
            const auto&     transform = m_Transforms[i];
            const glm::mat3 absoluteLinear {glm::abs(glm::vec3(transform[0])),
                                            glm::abs(glm::vec3(transform[1])),
                                            glm::abs(glm::vec3(transform[2]))};
            worldBounds[i]  = {glm::vec3(transform * glm::vec4(local.center, 1)), absoluteLinear * local.extent};
            const auto low  = worldBounds[i].center - worldBounds[i].extent;
            const auto high = worldBounds[i].center + worldBounds[i].extent;
            if (!finite(low) || !finite(high))
            {
                return std::unexpected("Transformed vertex-position bounds exceed finite coordinates");
            }
            minimum = glm::min(minimum, low);
            maximum = glm::max(maximum, high);
        }
        const auto nextCenter = minimum * .5f + maximum * .5f;
        const auto nextRadius = glm::length((glm::dvec3(maximum) - glm::dvec3(minimum)) * .5);
        if (nextRadius > std::numeric_limits<float>::max())
        {
            return std::unexpected("Vertex-position scene radius exceeds finite coordinates");
        }
        m_LocalBounds.assign(localBounds.begin(), localBounds.end());
        m_WorldBounds = std::move(worldBounds);
        center        = nextCenter;
        radius        = float(nextRadius);
        ++m_GeometryRevision;
        return {};
    }

    uint64_t GpuScene::geometryRevision() const
    {
        return m_GeometryRevision;
    }

    const PrimitiveBounds& GpuScene::primitiveBounds(uint32_t index) const
    {
        return m_WorldBounds.at(index);
    }

    std::expected<const PrimitiveBounds*, std::string> GpuScene::primitiveBoundsExpected(uint32_t index) const
    {
        if (index >= m_WorldBounds.size())
        {
            return std::unexpected("Primitive bounds index is out of range");
        }
        return &m_WorldBounds[index];
    }
} // namespace vultra
