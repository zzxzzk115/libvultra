#include "vultra/core/rhi/shader_reflection.hpp"

#include <cassert>

// Runtime-sized array fallback value used by legacy libvultra descriptor layout.
#define MAX_ARRAY_SIZE 1024

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] vk::ShaderStageFlags toVkStageFlags(const vshadersystem::ShaderStageFlags flags)
            {
                vk::ShaderStageFlags out = vk::ShaderStageFlagBits(0);

                using namespace vshadersystem;
                if (flags & ShaderStageFlagBits::eStageVert)
                    out |= vk::ShaderStageFlagBits::eVertex;
                if (flags & ShaderStageFlagBits::eStageFrag)
                    out |= vk::ShaderStageFlagBits::eFragment;
                if (flags & ShaderStageFlagBits::eStageComp)
                    out |= vk::ShaderStageFlagBits::eCompute;
                if (flags & ShaderStageFlagBits::eStageTask)
                    out |= vk::ShaderStageFlagBits::eTaskEXT;
                if (flags & ShaderStageFlagBits::eStageMesh)
                    out |= vk::ShaderStageFlagBits::eMeshEXT;

                if (flags & ShaderStageFlagBits::eStageRgen)
                    out |= vk::ShaderStageFlagBits::eRaygenKHR;
                if (flags & ShaderStageFlagBits::eStageRmiss)
                    out |= vk::ShaderStageFlagBits::eMissKHR;
                if (flags & ShaderStageFlagBits::eStageRchit)
                    out |= vk::ShaderStageFlagBits::eClosestHitKHR;
                if (flags & ShaderStageFlagBits::eStageRahit)
                    out |= vk::ShaderStageFlagBits::eAnyHitKHR;
                if (flags & ShaderStageFlagBits::eStageRint)
                    out |= vk::ShaderStageFlagBits::eIntersectionKHR;

                return out;
            }

            [[nodiscard]] vk::DescriptorType toVkDescriptorType(const vshadersystem::DescriptorKind k)
            {
                using DK = vshadersystem::DescriptorKind;
                switch (k)
                {
                    case DK::eUniformBuffer:
                        return vk::DescriptorType::eUniformBuffer;
                    case DK::eStorageBuffer:
                        return vk::DescriptorType::eStorageBuffer;
                    case DK::eSampledImage:
                        return vk::DescriptorType::eSampledImage;
                    case DK::eStorageImage:
                        return vk::DescriptorType::eStorageImage;
                    case DK::eSampler:
                        return vk::DescriptorType::eSampler;
                    case DK::eCombinedImageSampler:
                        return vk::DescriptorType::eCombinedImageSampler;
                    case DK::eAccelerationStructure:
                        return vk::DescriptorType::eAccelerationStructureKHR;
                    default:
                        return vk::DescriptorType::eSampler;
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

                auto [it, emplaced] = descriptorSets[d.set].try_emplace(d.binding, toVkDescriptorType(d.kind));
                auto& out           = it->second;

                if (emplaced)
                {
                    out.count = d.count;
                }
                out.stageFlags |= toVkStageFlags(d.stageFlags);

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

                vk::PushConstantRange range {};
                range.offset     = 0;
                range.size       = b.size;
                range.stageFlags = toVkStageFlags(b.stageFlags);

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
