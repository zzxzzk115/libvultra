#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/structs/descriptor_layout_flags.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr uint32_t kRuntimeSizedDescriptorUpperBound = 1024u;

            [[nodiscard]] vultra::rhi::ShaderStages toStages(const vshadersystem::ShaderStageFlags flags)
            {
                using vultra::rhi::ShaderStages;
                ShaderStages out {ShaderStages::eNone};

                using namespace vshadersystem;
                if (flags & ShaderStageFlagBits::eStageVert)
                    out |= ShaderStages::eVertex;
                if (flags & ShaderStageFlagBits::eStageFrag)
                    out |= ShaderStages::eFragment;
                if (flags & ShaderStageFlagBits::eStageComp)
                    out |= ShaderStages::eCompute;
                if (flags & ShaderStageFlagBits::eStageTask)
                    out |= ShaderStages::eTask;
                if (flags & ShaderStageFlagBits::eStageMesh)
                    out |= ShaderStages::eMesh;

                if (flags & ShaderStageFlagBits::eStageRgen)
                    out |= ShaderStages::eRayGen;
                if (flags & ShaderStageFlagBits::eStageRmiss)
                    out |= ShaderStages::eMiss;
                if (flags & ShaderStageFlagBits::eStageRchit)
                    out |= ShaderStages::eClosestHit;
                if (flags & ShaderStageFlagBits::eStageRahit)
                    out |= ShaderStages::eAnyHit;
                if (flags & ShaderStageFlagBits::eStageRint)
                    out |= ShaderStages::eIntersect;

                return out;
            }

            [[nodiscard]] vultra::rhi::DescriptorType toDescriptorType(const vshadersystem::DescriptorKind k)
            {
                using DK = vshadersystem::DescriptorKind;
                switch (k)
                {
                    case DK::eUniformBuffer:
                        return vultra::rhi::DescriptorType::eUniformBuffer;
                    case DK::eStorageBuffer:
                        return vultra::rhi::DescriptorType::eStorageBuffer;
                    case DK::eSampledImage:
                        return vultra::rhi::DescriptorType::eSampledImage;
                    case DK::eStorageImage:
                        return vultra::rhi::DescriptorType::eStorageImage;
                    case DK::eSampler:
                        return vultra::rhi::DescriptorType::eSampler;
                    case DK::eCombinedImageSampler:
                        return vultra::rhi::DescriptorType::eCombinedImageSampler;
                    case DK::eAccelerationStructure:
                        return vultra::rhi::DescriptorType::eAccelerationStructure;
                    default:
                        return vultra::rhi::DescriptorType::eSampler;
                }
            }
        } // namespace

        void ShaderReflection::accumulate(const vshadersystem::ShaderReflection& r)
        {
            // Local size
            if (r.hasLocalSize)
            {
                localSize = glm::uvec3 {r.localSizeX, r.localSizeY, r.localSizeZ};
            }

            // Descriptors
            for (const auto& d : r.descriptors)
            {
                if (d.set >= descriptorSets.size())
                    continue;

                auto [it, emplaced] = descriptorSets[d.set].try_emplace(d.binding, toDescriptorType(d.kind));
                auto& out           = it->second;

                if (emplaced)
                {
                    out.access = d.access;
                    out.count  = d.count;
                }
                out.access = d.access;
                out.stageFlags |= toStages(d.stageFlags);

                if (d.runtimeSized)
                {
                    out.count = kRuntimeSizedDescriptorUpperBound;
#ifdef __APPLE__
                    // Keep legacy MoltenVK behavior for runtime-sized descriptors.
                    out.flags = descriptor_layout_flags::eUpdateAfterBindPool;
#else
                    out.flags = descriptor_layout_flags::eVariableDescriptorCount;
#endif
                }
            }

            // Push constants
            for (const auto& b : r.blocks)
            {
                if (!b.isPushConstant)
                    continue;

                ShaderReflection::PushConstantRange range {};
                range.offset     = 0;
                range.size       = b.size;
                range.stageFlags = toStages(b.stageFlags);

                // Merge with existing ranges if they match (offset+size).
                bool merged = false;
                for (auto& existing : pushConstantRanges)
                {
                    if (existing.offset == range.offset && existing.size == range.size)
                    {
                        existing.stageFlags |= range.stageFlags;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                {
                    pushConstantRanges.emplace_back(range);
                }
            }
        }

        void ShaderReflection::accumulate(const ShaderReflection& r)
        {
            if (r.localSize.has_value())
            {
                localSize = r.localSize;
            }

            for (size_t set = 0; set < r.descriptorSets.size(); ++set)
            {
                for (const auto& [binding, descriptor] : r.descriptorSets[set])
                {
                    auto [it, emplaced] = descriptorSets[set].try_emplace(binding, descriptor.type);
                    auto& out           = it->second;
                    if (emplaced)
                    {
                        out.access = descriptor.access;
                        out.count  = descriptor.count;
                        out.flags  = descriptor.flags;
                    }
                    out.stageFlags |= descriptor.stageFlags;
                }
            }

            for (const auto& range : r.pushConstantRanges)
            {
                bool merged = false;
                for (auto& existing : pushConstantRanges)
                {
                    if (existing.offset == range.offset && existing.size == range.size)
                    {
                        existing.stageFlags |= range.stageFlags;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                {
                    pushConstantRanges.push_back(range);
                }
            }
        }
    } // namespace rhi
} // namespace vultra
