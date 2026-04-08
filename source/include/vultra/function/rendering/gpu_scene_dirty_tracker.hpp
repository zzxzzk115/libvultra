#pragma once

#include "vultra/function/rendering/render_structs.hpp"

#include <cstddef>
#include <cstdint>

namespace vultra
{
    class GpuSceneDirtyTracker
    {
    public:
        [[nodiscard]] bool shouldRebuild(const RenderWorld& world,
                                         uint64_t           resourceRevision,
                                         bool               gpuDrivenMeshletPipelineEnabled) const
        {
            if (!m_HasValidSnapshot)
                return true;

            const uint64_t worldSignature = hashRenderWorld(world);
            return worldSignature != m_LastCookedWorldSignature ||
                   resourceRevision != m_LastGpuResourceRevision ||
                   gpuDrivenMeshletPipelineEnabled != m_LastGpuDrivenMeshletPipelineEnabled;
        }

        void markBuilt(const RenderWorld& world,
                       uint64_t           resourceRevision,
                       bool               gpuDrivenMeshletPipelineEnabled)
        {
            m_LastCookedWorldSignature            = hashRenderWorld(world);
            m_LastGpuResourceRevision             = resourceRevision;
            m_LastGpuDrivenMeshletPipelineEnabled = gpuDrivenMeshletPipelineEnabled;
            m_HasValidSnapshot                    = true;
        }

        void reset()
        {
            m_LastCookedWorldSignature            = 0;
            m_LastGpuResourceRevision             = 0;
            m_LastGpuDrivenMeshletPipelineEnabled = true;
            m_HasValidSnapshot                    = false;
        }

    private:
        [[nodiscard]] static uint64_t fnv1a64(const void* data, size_t size, uint64_t seed)
        {
            constexpr uint64_t kPrime = 1099511628211ull;

            const auto* bytes = static_cast<const uint8_t*>(data);
            uint64_t    hash  = seed;
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kPrime;
            }
            return hash;
        }

        [[nodiscard]] static uint64_t hashRenderWorld(const RenderWorld& world)
        {
            constexpr uint64_t kOffsetBasis = 1469598103934665603ull;

            uint64_t hash = kOffsetBasis;

            const auto instanceCount = static_cast<uint64_t>(world.instances.size());
            hash                     = fnv1a64(&instanceCount, sizeof(instanceCount), hash);
            for (const auto& inst : world.instances)
            {
                hash = fnv1a64(&inst.meshIndex, sizeof(inst.meshIndex), hash);
                hash = fnv1a64(&inst.materialIndex, sizeof(inst.materialIndex), hash);
                hash = fnv1a64(&inst.worldMatrix, sizeof(inst.worldMatrix), hash);
            }

            const auto splatCount = static_cast<uint64_t>(world.splatInstances.size());
            hash                  = fnv1a64(&splatCount, sizeof(splatCount), hash);
            for (const auto& inst : world.splatInstances)
            {
                hash = fnv1a64(&inst.splatIndex, sizeof(inst.splatIndex), hash);
                hash = fnv1a64(&inst.worldMatrix, sizeof(inst.worldMatrix), hash);
            }

            return hash;
        }

    private:
        uint64_t m_LastCookedWorldSignature {0};
        uint64_t m_LastGpuResourceRevision {0};
        bool     m_LastGpuDrivenMeshletPipelineEnabled {true};
        bool     m_HasValidSnapshot {false};
    };
} // namespace vultra
