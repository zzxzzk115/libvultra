#include "vultra/core/rhi/shader_reflection.hpp"

#include <cassert>

#include <vulkan/vulkan.hpp>

// Runtime-sized array fallback value used by legacy libvultra descriptor layout.
#define MAX_ARRAY_SIZE 1024

namespace vultra
{
    namespace rhi
    {
namespace
{
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
                    out.count = d.count;
                }
                out.stageFlags |= toStages(d.stageFlags);

                if (d.runtimeSized)
                {
                    out.count = MAX_ARRAY_SIZE;
#ifdef __APPLE__
                    // On macOS, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT is not supported.
                    // Keep legacy behavior to avoid validation errors.
                    out.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
#else
                    out.flags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
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
    } // namespace rhi
} // namespace vultra
