#include "ray_geometry.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/ray_scene.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace vultra
{
    namespace
    {
        struct BuildBufferState
        {
            Buffer&       buffer;
            Buffer::State before;
        };

        std::expected<void, std::string>
        discardBuildExpected(Frame& frame, std::span<const BuildBufferState> buffers, std::string error)
        {
            if (auto discarded = frame.discardExpected(); !discarded)
            {
                return std::unexpected(error + "; discard AS recording: " + discarded.error());
            }
            for (const auto& state : buffers)
            {
                if (auto restored = state.buffer.restoreUnsubmittedExpected(state.before); !restored)
                {
                    return std::unexpected(error + "; restore AS input: " + restored.error());
                }
            }
            return std::unexpected(std::move(error));
        }

        std::expected<void, std::string> submitBuildExpected(Frame& frame, std::span<const BuildBufferState> buffers)
        {
            if (auto submitted = frame.submitExpected(); !submitted)
            {
                return discardBuildExpected(frame, buffers, submitted.error());
            }
            return frame.waitExpected();
        }
    } // namespace

    detail::RayGeometry::~RayGeometry()
    {
        device.waitIdleForDestruction();
        for (auto* bottom : bottoms)
        {
            if (bottom)
            {
                ray.DestroyAccelerationStructure(bottom);
            }
        }
    }

    std::expected<void, std::string> detail::RayGeometry::initializeExpected()
    {
        vertexBuffer = scene.vertices->handle;
        indexBuffer  = scene.indices->handle;
        primitives   = scene.primitives;
        geometries.resize(primitives.size());
        descriptions.resize(primitives.size());
        bottoms.resize(primitives.size(), nullptr);
        if (scene.vertices->desc.size / sizeof(SceneVertex) > std::numeric_limits<uint32_t>::max())
        {
            return std::unexpected("Ray scene vertex count exceeds VRI limits");
        }
        for (size_t i = 0; i < primitives.size(); ++i)
        {
            const auto& primitive = primitives[i];
            if (!primitive.indexCount || primitive.indexCount % 3 || primitive.material >= scene.materials.size() ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices->desc.size / sizeof(uint32_t))
            {
                return std::unexpected("Ray scene contains invalid indexed geometry or material");
            }
            auto& geometry                  = geometries[i];
            geometry.type                   = VriAsGeometryType_Triangles;
            geometry.flags                  = VriAsGeometry_None;
            geometry.triangles.vertexBuffer = vertexBuffer;
            geometry.triangles.vertexCount  = uint32_t(scene.vertices->desc.size / sizeof(SceneVertex));
            geometry.triangles.vertexStride = sizeof(SceneVertex);
            geometry.triangles.vertexFormat = VriFormat_RGB32_SFLOAT;
            geometry.triangles.indexBuffer  = indexBuffer;
            geometry.triangles.indexOffset  = uint64_t(primitive.firstIndex) * sizeof(uint32_t);
            geometry.triangles.indexCount   = primitive.indexCount;
            geometry.triangles.indexType    = VriIndexType_UInt32;
            descriptions[i]                 = {VriAccelerationStructureType_BottomLevel,
                                               VriAccelerationStructureBuild_PreferFastTrace,
                                               1,
                                               &geometry};
        }
        return updateExpected();
    }

    std::expected<void, std::string> detail::RayGeometry::updateExpected()
    {
        if (auto valid = validateExpected(); !valid)
        {
            return valid;
        }
        if (completedRevision == scene.geometryRevision())
        {
            return {};
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        Logger::core().info("Ray geometry: building {} shared BLAS at revision {}",
                            bottoms.size(),
                            scene.geometryRevision());
        // Bound native submissions on mobile. Only completed builds release their transient scratch.
        constexpr size_t buildBatchSize = 32;
        for (size_t first = 0; first < bottoms.size(); first += buildBatchSize)
        {
            const size_t end = std::min(first + buildBatchSize, bottoms.size());
            for (size_t i = first; i < end; ++i)
            {
                if (!bottoms[i])
                {
                    const auto status = ray.CreateAccelerationStructure(device.handle, &descriptions[i], &bottoms[i]);
                    if (status != VriResult_Success || !bottoms[i])
                    {
                        return std::unexpected("Create ray scene BLAS failed: " + std::to_string(status));
                    }
                }
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            const std::array states {BuildBufferState {*scene.vertices, scene.vertices->captureState()},
                                     BuildBufferState {*scene.indices, scene.indices->captureState()}};
            if (first == 0)
            {
                const VriAccessStage next {VriAccess_AccelerationStructureRead | VriAccess_ShaderResourceRead,
                                           VriPipelineStage_AccelerationStructureBuild |
                                               VriPipelineStage_ComputeShader};
                for (const auto& state : states)
                {
                    if (auto transitioned = state.buffer.transitionExpected(*commands, next); !transitioned)
                    {
                        return discardBuildExpected(**frame, states, transitioned.error());
                    }
                }
            }
            for (size_t i = first; i < end; ++i)
            {
                const VriBuildAccelerationStructureDesc desc {bottoms[i], &descriptions[i]};
                ray.CmdBuildAccelerationStructure(*commands, &desc);
            }
            if (auto completed = submitBuildExpected(**frame, states); !completed)
            {
                return completed;
            }
            if (ray.ReleaseAccelerationStructureScratch)
            {
                for (size_t i = first; i < end; ++i)
                {
                    ray.ReleaseAccelerationStructureScratch(bottoms[i]);
                }
            }
        }
        completedRevision = scene.geometryRevision();
        return {};
    }

    std::expected<void, std::string> detail::RayGeometry::validateExpected() const
    {
        if (!scene.vertices || !scene.indices || scene.vertices->handle != vertexBuffer ||
            scene.indices->handle != indexBuffer)
        {
            return std::unexpected("Ray scene geometry buffers changed; replace the GPU scene");
        }
        if (scene.primitives.size() != primitives.size())
        {
            return std::unexpected("Ray scene topology changed; rebuild the acceleration structure");
        }
        for (size_t i = 0; i < primitives.size(); ++i)
        {
            const auto& current  = scene.primitives[i];
            const auto& original = primitives[i];
            if (current.firstIndex != original.firstIndex || current.indexCount != original.indexCount ||
                current.material != original.material || current.material >= scene.materials.size())
            {
                return std::unexpected("Ray scene topology changed; rebuild the acceleration structure");
            }
        }
        return {};
    }

    std::expected<RayScene*, std::string> GpuScene::raySceneExpected()
    {
        if (m_RayScene)
        {
            if (auto updated = m_RayScene->update(); !updated)
            {
                return std::unexpected(updated.error());
            }
            return m_RayScene.get();
        }
        auto rays = RayScene::create(m_Device, *this);
        if (!rays)
        {
            return std::unexpected(rays.error());
        }
        m_RayScene = std::move(*rays);
        return m_RayScene.get();
    }

    std::expected<std::unique_ptr<RayScene>, std::string>
    RayScene::create(Device& device, GpuScene& scene, RaySceneOptions options)
    {
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!scene.belongsTo(device) || !caps || !caps->hasRayQuery || !scene.vertices || !scene.indices ||
            !(scene.vertices->desc.usage & VriBufferUsage_AccelerationBuildInput) ||
            !(scene.indices->desc.usage & VriBufferUsage_AccelerationBuildInput) || scene.primitives.size() > 0xffffffu)
        {
            return std::unexpected("Ray scene requires ray query, compatible geometry and 24-bit primitive IDs");
        }
        if (options.forceOpaque && options.forceNonOpaque)
        {
            return std::unexpected("Ray scene cannot force both opaque and non-opaque instances");
        }
        VriRayTracingInterface ray {};
        const auto             status = vriGetInterface(device.handle, VRI_INTERFACE_RAYTRACING, sizeof(ray), &ray);
        if (status != VriResult_Success)
        {
            return std::unexpected("Get ray tracing interface failed: " + std::to_string(status));
        }
        std::unique_ptr<detail::RayGeometry> candidate;
        auto*                                geometry = scene.m_RayGeometry.get();
        if (!geometry)
        {
            candidate = std::make_unique<detail::RayGeometry>(device, scene, ray);
            if (auto initialized = candidate->initializeExpected(); !initialized)
            {
                return std::unexpected(initialized.error());
            }
            geometry = candidate.get();
        }
        else if (auto valid = geometry->validateExpected(); !valid)
        {
            return std::unexpected(valid.error());
        }
        auto result = std::unique_ptr<RayScene>(new RayScene(device, scene, *geometry, options, ray));
        if (auto initialized = result->initialize(); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        // Publish the shared owner only after the first borrowing TLAS and descriptor are complete.
        if (candidate)
        {
            scene.m_RayGeometry = std::move(candidate);
        }
        return result;
    }

    RayScene::~RayScene()
    {
        m_Device.waitIdleForDestruction();
        if (m_Descriptor)
        {
            m_Device.core.DestroyDescriptor(m_Descriptor);
        }
        if (m_Top)
        {
            m_Ray.DestroyAccelerationStructure(m_Top);
        }
    }

    std::expected<void, std::string> RayScene::initialize()
    {
        m_Instances.resize(std::max<size_t>(m_Geometry.primitives.size(), 1));
        for (size_t i = 0; i < m_Geometry.bottoms.size(); ++i)
        {
            m_Instances[i].address = m_Ray.GetAccelerationStructureDeviceAddress(m_Geometry.bottoms[i]);
        }
        auto instances = Buffer::create(m_Device,
                                        {m_Instances.size() * sizeof(Instance),
                                         0,
                                         VriBufferUsage_AccelerationBuildInput,
                                         VriMemoryLocation_HostUpload});
        if (!instances)
        {
            return std::unexpected(instances.error());
        }
        m_InstanceBuffer                       = std::move(*instances);
        m_TopGeometry.type                     = VriAsGeometryType_Instances;
        m_TopGeometry.instances.instanceBuffer = m_InstanceBuffer->handle;
        m_TopGeometry.instances.instanceCount  = uint32_t(m_Geometry.primitives.size());
        m_TopDescription                       = {VriAccelerationStructureType_TopLevel,
                                                  VriAccelerationStructureBuild_PreferFastTrace,
                                                  1,
                                                  &m_TopGeometry};
        auto status = m_Ray.CreateAccelerationStructure(m_Device.handle, &m_TopDescription, &m_Top);
        if (status != VriResult_Success || !m_Top)
        {
            return std::unexpected("Create ray scene TLAS failed: " + std::to_string(status));
        }
        if (auto updated = update(); !updated)
        {
            return updated;
        }
        status = m_Ray.CreateAccelerationStructureDescriptor(m_Device.handle, m_Top, &m_Descriptor);
        if (status != VriResult_Success || !m_Descriptor)
        {
            return std::unexpected("Create ray scene descriptor failed: " + std::to_string(status));
        }
        return {};
    }

    std::expected<void, std::string> RayScene::update()
    {
        if (auto valid = m_Geometry.updateExpected(); !valid)
        {
            return valid;
        }
        for (size_t i = 0; i < m_Geometry.primitives.size(); ++i)
        {
            const auto& current   = m_Scene.primitives[i];
            auto transform = m_Scene.primitiveTransformExpected(uint32_t(i));
            if (!transform)
            {
                return std::unexpected(transform.error());
            }
            for (uint32_t row = 0; row < 3; ++row)
            {
                for (uint32_t column = 0; column < 4; ++column)
                {
                    m_Instances[i].transform[row * 4 + column] = (**transform)[column][row];
                }
            }
            const auto&        material       = m_Scene.materials[current.material];
            constexpr uint32_t disableCulling = 1;
            constexpr uint32_t forceOpaque    = 4;
            constexpr uint32_t forceNonOpaque = 8;
            uint32_t           flags = (material.doubleSided || m_Options.disableFaceCulling) ? disableCulling : 0u;
            if (m_Options.forceNonOpaque)
            {
                flags |= forceNonOpaque;
            }
            else if (m_Options.forceOpaque || (!material.alphaBlend && material.alphaCutoff < 0))
            {
                flags |= forceOpaque;
            }
            m_Instances[i].idAndMask =
                uint32_t(i) | ((material.alphaBlend && !m_Options.includeAlphaBlended) ? 0u : 0xffu << 24);
            m_Instances[i].offsetAndFlags = flags << 24;
        }
        if (m_CompletedGeometryRevision == m_Geometry.completedRevision &&
            m_CompletedInstances.size() == m_Instances.size() &&
            std::memcmp(m_CompletedInstances.data(), m_Instances.data(), m_Instances.size() * sizeof(Instance)) == 0)
        {
            return {};
        }
        auto* mapped = m_Device.core.MapBuffer(m_InstanceBuffer->handle, 0, m_Instances.size() * sizeof(Instance));
        if (!mapped)
        {
            return std::unexpected("Map ray scene instances failed");
        }
        std::memcpy(mapped, m_Instances.data(), m_Instances.size() * sizeof(Instance));
        m_Device.core.UnmapBuffer(m_InstanceBuffer->handle);
        auto frame = Frame::create(m_Device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const std::array states {BuildBufferState {*m_InstanceBuffer, m_InstanceBuffer->captureState()}};
        if (auto transitioned = m_InstanceBuffer->transitionExpected(
                *commands,
                {VriAccess_AccelerationStructureRead, VriPipelineStage_AccelerationStructureBuild});
            !transitioned)
        {
            return discardBuildExpected(**frame, states, transitioned.error());
        }
        const VriBuildAccelerationStructureDesc desc {m_Top, &m_TopDescription};
        m_Ray.CmdBuildAccelerationStructure(*commands, &desc);
        if (auto completed = submitBuildExpected(**frame, states); !completed)
        {
            return completed;
        }
        m_CompletedInstances = m_Instances;
        m_CompletedGeometryRevision = m_Geometry.completedRevision;
        return {};
    }
} // namespace vultra
