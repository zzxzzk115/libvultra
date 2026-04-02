#pragma once

#include "vultra/core/rhi/structs/access.hpp"
#include "vultra/core/rhi/structs/compare_op.hpp"
#include "vultra/core/rhi/structs/descriptor_type.hpp"
#include "vultra/core/rhi/structs/cull_mode.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/offset2d.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/pipeline_bind_point.hpp"
#include "vultra/core/rhi/structs/pipeline_stage.hpp"
#include "vultra/core/rhi/structs/rect2d.hpp"
#include "vultra/core/rhi/structs/sampler_info.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/core/rhi/structs/texel_filter.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra::rhi
{
    [[nodiscard]] constexpr vk::Extent2D toVk(Extent2D extent)
    {
        return {extent.width, extent.height};
    }

    [[nodiscard]] constexpr vk::Offset2D toVk(Offset2D offset)
    {
        return {offset.x, offset.y};
    }

    [[nodiscard]] constexpr vk::Rect2D toVk(Rect2D rect)
    {
        return {toVk(rect.offset), toVk(rect.extent)};
    }

    [[nodiscard]] constexpr vk::ImageAspectFlags toVk(ImageAspect imageAspect)
    {
        switch (imageAspect)
        {
            case ImageAspect::eColor:
                return vk::ImageAspectFlagBits::eColor;
            case ImageAspect::eDepth:
                return vk::ImageAspectFlagBits::eDepth;
            case ImageAspect::eStencil:
                return vk::ImageAspectFlagBits::eStencil;
            case ImageAspect::eNone:
                return vk::ImageAspectFlagBits::eNone;
        }
        return vk::ImageAspectFlagBits::eNone;
    }

    [[nodiscard]] constexpr vk::ImageAspectFlags toVk(ImageAspectFlags aspectMask)
    {
        vk::ImageAspectFlags out {};
        if (HasFlagValues(aspectMask, ImageAspectFlags::eColor))
            out |= vk::ImageAspectFlagBits::eColor;
        if (HasFlagValues(aspectMask, ImageAspectFlags::eDepth))
            out |= vk::ImageAspectFlagBits::eDepth;
        if (HasFlagValues(aspectMask, ImageAspectFlags::eStencil))
            out |= vk::ImageAspectFlagBits::eStencil;
        return out;
    }

    [[nodiscard]] constexpr ImageAspectFlags toRhi(vk::ImageAspectFlags aspectMask)
    {
        ImageAspectFlags out {ImageAspectFlags::eNone};
        if (aspectMask & vk::ImageAspectFlagBits::eColor)
            out |= ImageAspectFlags::eColor;
        if (aspectMask & vk::ImageAspectFlagBits::eDepth)
            out |= ImageAspectFlags::eDepth;
        if (aspectMask & vk::ImageAspectFlagBits::eStencil)
            out |= ImageAspectFlags::eStencil;
        return out;
    }

    [[nodiscard]] vk::Format   toVk(PixelFormat);
    [[nodiscard]] PixelFormat  fromVk(vk::Format);

    [[nodiscard]] constexpr vk::CompareOp toVk(CompareOp op)
    {
        switch (op)
        {
            case CompareOp::eNever:
                return vk::CompareOp::eNever;
            case CompareOp::eLess:
                return vk::CompareOp::eLess;
            case CompareOp::eEqual:
                return vk::CompareOp::eEqual;
            case CompareOp::eLessOrEqual:
                return vk::CompareOp::eLessOrEqual;
            case CompareOp::eGreater:
                return vk::CompareOp::eGreater;
            case CompareOp::eNotEqual:
                return vk::CompareOp::eNotEqual;
            case CompareOp::eGreaterOrEqual:
                return vk::CompareOp::eGreaterOrEqual;
            case CompareOp::eAlways:
                return vk::CompareOp::eAlways;
        }
        return vk::CompareOp::eNever;
    }

    [[nodiscard]] constexpr vk::Filter toVk(TexelFilter filter)
    {
        switch (filter)
        {
            case TexelFilter::eNearest:
                return vk::Filter::eNearest;
            case TexelFilter::eLinear:
                return vk::Filter::eLinear;
        }
        return vk::Filter::eNearest;
    }

    [[nodiscard]] constexpr vk::SamplerMipmapMode toVk(MipmapMode mode)
    {
        switch (mode)
        {
            case MipmapMode::eNearest:
                return vk::SamplerMipmapMode::eNearest;
            case MipmapMode::eLinear:
                return vk::SamplerMipmapMode::eLinear;
        }
        return vk::SamplerMipmapMode::eNearest;
    }

    [[nodiscard]] constexpr vk::SamplerAddressMode toVk(SamplerAddressMode mode)
    {
        switch (mode)
        {
            case SamplerAddressMode::eRepeat:
                return vk::SamplerAddressMode::eRepeat;
            case SamplerAddressMode::eMirroredRepeat:
                return vk::SamplerAddressMode::eMirroredRepeat;
            case SamplerAddressMode::eClampToEdge:
                return vk::SamplerAddressMode::eClampToEdge;
            case SamplerAddressMode::eClampToBorder:
                return vk::SamplerAddressMode::eClampToBorder;
            case SamplerAddressMode::eMirrorClampToEdge:
                return vk::SamplerAddressMode::eMirrorClampToEdge;
        }
        return vk::SamplerAddressMode::eRepeat;
    }

    [[nodiscard]] constexpr vk::BorderColor toVk(BorderColor color)
    {
        switch (color)
        {
            case BorderColor::eFloatTransparentBlack:
                return vk::BorderColor::eFloatTransparentBlack;
            case BorderColor::eFloatOpaqueBlack:
                return vk::BorderColor::eFloatOpaqueBlack;
            case BorderColor::eFloatOpaqueWhite:
                return vk::BorderColor::eFloatOpaqueWhite;
        }
        return vk::BorderColor::eFloatOpaqueBlack;
    }

    [[nodiscard]] constexpr vk::ImageLayout toVk(ImageLayout layout)
    {
        switch (layout)
        {
            case ImageLayout::eUndefined:
                return vk::ImageLayout::eUndefined;
            case ImageLayout::eGeneral:
                return vk::ImageLayout::eGeneral;
            case ImageLayout::eAttachment:
                return vk::ImageLayout::eAttachmentOptimal;
            case ImageLayout::eReadOnly:
                return vk::ImageLayout::eReadOnlyOptimal;
            case ImageLayout::eTransferSrc:
                return vk::ImageLayout::eTransferSrcOptimal;
            case ImageLayout::eTransferDst:
                return vk::ImageLayout::eTransferDstOptimal;
            case ImageLayout::ePresent:
                return vk::ImageLayout::ePresentSrcKHR;
        }
        return vk::ImageLayout::eUndefined;
    }

    [[nodiscard]] constexpr ImageLayout fromVk(vk::ImageLayout layout)
    {
        switch (layout)
        {
            case vk::ImageLayout::eUndefined:
                return ImageLayout::eUndefined;
            case vk::ImageLayout::eGeneral:
                return ImageLayout::eGeneral;
            case vk::ImageLayout::eAttachmentOptimal:
            case vk::ImageLayout::eColorAttachmentOptimal:
            case vk::ImageLayout::eDepthStencilAttachmentOptimal:
                return ImageLayout::eAttachment;
            case vk::ImageLayout::eReadOnlyOptimal:
            case vk::ImageLayout::eShaderReadOnlyOptimal:
            case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
                return ImageLayout::eReadOnly;
            case vk::ImageLayout::eTransferSrcOptimal:
                return ImageLayout::eTransferSrc;
            case vk::ImageLayout::eTransferDstOptimal:
                return ImageLayout::eTransferDst;
            case vk::ImageLayout::ePresentSrcKHR:
                return ImageLayout::ePresent;
            case vk::ImageLayout::ePreinitialized:
                // Image starts undefined from RHI point of view until first explicit barrier.
                return ImageLayout::eUndefined;
            default:
                break;
        }
        return ImageLayout::eUndefined;
    }

    [[nodiscard]] constexpr vk::PipelineBindPoint toVk(PipelineBindPoint bindPoint)
    {
        switch (bindPoint)
        {
            case PipelineBindPoint::eGraphics:
                return vk::PipelineBindPoint::eGraphics;
            case PipelineBindPoint::eCompute:
                return vk::PipelineBindPoint::eCompute;
            case PipelineBindPoint::eRayTracing:
                return vk::PipelineBindPoint::eRayTracingKHR;
        }
        return vk::PipelineBindPoint::eGraphics;
    }

    [[nodiscard]] constexpr vk::ShaderStageFlags toVk(ShaderStages stages)
    {
        vk::ShaderStageFlags out {};
        if (static_cast<bool>(stages & ShaderStages::eVertex))
            out |= vk::ShaderStageFlagBits::eVertex;
        if (static_cast<bool>(stages & ShaderStages::eGeometry))
            out |= vk::ShaderStageFlagBits::eGeometry;
        if (static_cast<bool>(stages & ShaderStages::eFragment))
            out |= vk::ShaderStageFlagBits::eFragment;
        if (static_cast<bool>(stages & ShaderStages::eCompute))
            out |= vk::ShaderStageFlagBits::eCompute;
        if (static_cast<bool>(stages & ShaderStages::eRayGen))
            out |= vk::ShaderStageFlagBits::eRaygenKHR;
        if (static_cast<bool>(stages & ShaderStages::eMiss))
            out |= vk::ShaderStageFlagBits::eMissKHR;
        if (static_cast<bool>(stages & ShaderStages::eClosestHit))
            out |= vk::ShaderStageFlagBits::eClosestHitKHR;
        if (static_cast<bool>(stages & ShaderStages::eAnyHit))
            out |= vk::ShaderStageFlagBits::eAnyHitKHR;
        if (static_cast<bool>(stages & ShaderStages::eIntersect))
            out |= vk::ShaderStageFlagBits::eIntersectionKHR;
        if (static_cast<bool>(stages & ShaderStages::eMesh))
            out |= vk::ShaderStageFlagBits::eMeshEXT;
        if (static_cast<bool>(stages & ShaderStages::eTask))
            out |= vk::ShaderStageFlagBits::eTaskEXT;
        return out;
    }

    [[nodiscard]] constexpr vk::CullModeFlags toVk(CullMode mode)
    {
        switch (mode)
        {
            case CullMode::eNone:
                return vk::CullModeFlagBits::eNone;
            case CullMode::eFront:
                return vk::CullModeFlagBits::eFront;
            case CullMode::eBack:
                return vk::CullModeFlagBits::eBack;
        }
        return vk::CullModeFlagBits::eNone;
    }

    [[nodiscard]] constexpr vk::AccessFlags2 toVk(Access access)
    {
        vk::AccessFlags2 out {};
        if (static_cast<bool>(access & Access::eIndexRead))
            out |= vk::AccessFlagBits2::eIndexRead;
        if (static_cast<bool>(access & Access::eVertexAttributeRead))
            out |= vk::AccessFlagBits2::eVertexAttributeRead;
        if (static_cast<bool>(access & Access::eIndirectCommandRead))
            out |= vk::AccessFlagBits2::eIndirectCommandRead;
        if (static_cast<bool>(access & Access::eUniformRead))
            out |= vk::AccessFlagBits2::eUniformRead;
        if (static_cast<bool>(access & Access::eShaderRead))
            out |= vk::AccessFlagBits2::eShaderRead;
        if (static_cast<bool>(access & Access::eShaderWrite))
            out |= vk::AccessFlagBits2::eShaderWrite;
        if (static_cast<bool>(access & Access::eColorAttachmentRead))
            out |= vk::AccessFlagBits2::eColorAttachmentRead;
        if (static_cast<bool>(access & Access::eColorAttachmentWrite))
            out |= vk::AccessFlagBits2::eColorAttachmentWrite;
        if (static_cast<bool>(access & Access::eDepthStencilAttachmentRead))
            out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        if (static_cast<bool>(access & Access::eDepthStencilAttachmentWrite))
            out |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        if (static_cast<bool>(access & Access::eTransferRead))
            out |= vk::AccessFlagBits2::eTransferRead;
        if (static_cast<bool>(access & Access::eTransferWrite))
            out |= vk::AccessFlagBits2::eTransferWrite;
        if (static_cast<bool>(access & Access::eMemoryRead))
            out |= vk::AccessFlagBits2::eMemoryRead;
        if (static_cast<bool>(access & Access::eMemoryWrite))
            out |= vk::AccessFlagBits2::eMemoryWrite;
        if (static_cast<bool>(access & Access::eShaderStorageRead))
            out |= vk::AccessFlagBits2::eShaderStorageRead;
        if (static_cast<bool>(access & Access::eShaderStorageWrite))
            out |= vk::AccessFlagBits2::eShaderStorageWrite;
        if (static_cast<bool>(access & Access::eAccelerationStructureRead))
            out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR;
        if (static_cast<bool>(access & Access::eAccelerationStructureWrite))
            out |= vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        return out;
    }

    [[nodiscard]] constexpr vk::PipelineStageFlags2 toVk(PipelineStages stages)
    {
        vk::PipelineStageFlags2 out {};
        if (static_cast<bool>(stages & PipelineStages::eTop))
            out |= vk::PipelineStageFlagBits2::eTopOfPipe;
        if (static_cast<bool>(stages & PipelineStages::eDrawIndirect))
            out |= vk::PipelineStageFlagBits2::eDrawIndirect;
        if (static_cast<bool>(stages & PipelineStages::eVertexInput))
            out |= vk::PipelineStageFlagBits2::eVertexInput;
        if (static_cast<bool>(stages & PipelineStages::eVertexShader))
            out |= vk::PipelineStageFlagBits2::eVertexShader;
        if (static_cast<bool>(stages & PipelineStages::eGeometryShader))
            out |= vk::PipelineStageFlagBits2::eGeometryShader;
        if (static_cast<bool>(stages & PipelineStages::eFragmentShader))
            out |= vk::PipelineStageFlagBits2::eFragmentShader;
        if (static_cast<bool>(stages & PipelineStages::eEarlyFragmentTest))
            out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests;
        if (static_cast<bool>(stages & PipelineStages::eLateFragmentTest))
            out |= vk::PipelineStageFlagBits2::eLateFragmentTests;
        if (static_cast<bool>(stages & PipelineStages::eColorAttachmentOutput))
            out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        if (static_cast<bool>(stages & PipelineStages::eComputeShader))
            out |= vk::PipelineStageFlagBits2::eComputeShader;
        if (static_cast<bool>(stages & PipelineStages::eRayTracingShader))
            out |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        if (static_cast<bool>(stages & PipelineStages::eAccelerationStructureBuild))
            out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        if (static_cast<bool>(stages & PipelineStages::eTransfer))
            out |= vk::PipelineStageFlagBits2::eTransfer;
        if (static_cast<bool>(stages & PipelineStages::eBlit))
            out |= vk::PipelineStageFlagBits2::eBlit;
        if (static_cast<bool>(stages & PipelineStages::eBottom))
            out |= vk::PipelineStageFlagBits2::eBottomOfPipe;
        if (static_cast<bool>(stages & PipelineStages::eAllTransfer))
            out |= vk::PipelineStageFlagBits2::eAllTransfer;
        if (static_cast<bool>(stages & PipelineStages::eAllGraphics))
            out |= vk::PipelineStageFlagBits2::eAllGraphics;
        if (static_cast<bool>(stages & PipelineStages::eAllCommands))
            out |= vk::PipelineStageFlagBits2::eAllCommands;
        return out;
    }

    [[nodiscard]] constexpr vk::DescriptorType toVk(DescriptorType type)
    {
        switch (type)
        {
            case DescriptorType::eSampler:
                return vk::DescriptorType::eSampler;
            case DescriptorType::eCombinedImageSampler:
                return vk::DescriptorType::eCombinedImageSampler;
            case DescriptorType::eSampledImage:
                return vk::DescriptorType::eSampledImage;
            case DescriptorType::eStorageImage:
                return vk::DescriptorType::eStorageImage;
            case DescriptorType::eUniformBuffer:
                return vk::DescriptorType::eUniformBuffer;
            case DescriptorType::eStorageBuffer:
                return vk::DescriptorType::eStorageBuffer;
            case DescriptorType::eInputAttachment:
                return vk::DescriptorType::eInputAttachment;
            case DescriptorType::eStorageBufferDynamic:
                return vk::DescriptorType::eStorageBufferDynamic;
            case DescriptorType::eAccelerationStructure:
                return vk::DescriptorType::eAccelerationStructureKHR;
        }
        return vk::DescriptorType::eSampler;
    }

} // namespace vultra::rhi
