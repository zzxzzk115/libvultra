#pragma once

#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/image_usage.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/primitive_topology.hpp"
#include "vultra/core/rhi/structs/compare_op.hpp"
#include "vultra/core/rhi/structs/cull_mode.hpp"
#include "vultra/core/rhi/structs/sampler_info.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/core/rhi/structs/swapchain_format.hpp"
#include "vultra/core/rhi/structs/texel_filter.hpp"
#include "vultra/core/rhi/structs/texture_type.hpp"
#include "vultra/core/rhi/structs/vertical_sync.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>

namespace vultra::rhi::webgpu
{
    [[nodiscard]] constexpr WGPUPrimitiveTopology toWgpuPrimitiveTopology(const PrimitiveTopology topology)
    {
        switch (topology)
        {
            case PrimitiveTopology::eTriangleList:
                return WGPUPrimitiveTopology_TriangleList;
            case PrimitiveTopology::eTriangleStrip:
                return WGPUPrimitiveTopology_TriangleStrip;
            case PrimitiveTopology::eLineList:
                return WGPUPrimitiveTopology_LineList;
            case PrimitiveTopology::eLineStrip:
                return WGPUPrimitiveTopology_LineStrip;
            case PrimitiveTopology::ePointList:
                return WGPUPrimitiveTopology_PointList;
        }
        return WGPUPrimitiveTopology_TriangleList;
    }

    [[nodiscard]] constexpr WGPUCompareFunction toWgpuCompareFunction(const CompareOp op)
    {
        switch (op)
        {
            case CompareOp::eNever:
                return WGPUCompareFunction_Never;
            case CompareOp::eLess:
                return WGPUCompareFunction_Less;
            case CompareOp::eEqual:
                return WGPUCompareFunction_Equal;
            case CompareOp::eLessOrEqual:
                return WGPUCompareFunction_LessEqual;
            case CompareOp::eGreater:
                return WGPUCompareFunction_Greater;
            case CompareOp::eNotEqual:
                return WGPUCompareFunction_NotEqual;
            case CompareOp::eGreaterOrEqual:
                return WGPUCompareFunction_GreaterEqual;
            case CompareOp::eAlways:
            default:
                return WGPUCompareFunction_Always;
        }
    }

    [[nodiscard]] constexpr WGPUCullMode toWgpuCullMode(const CullMode mode)
    {
        switch (mode)
        {
            case CullMode::eFront:
                return WGPUCullMode_Front;
            case CullMode::eBack:
                return WGPUCullMode_Back;
            case CullMode::eNone:
            default:
                return WGPUCullMode_None;
        }
    }

    [[nodiscard]] constexpr WGPUTextureFormat toWgpuTextureFormat(const PixelFormat format)
    {
        switch (format)
        {
            case PixelFormat::eBGRA8_UNorm:
                return WGPUTextureFormat_BGRA8Unorm;
            case PixelFormat::eBGRA8_sRGB:
                return WGPUTextureFormat_BGRA8UnormSrgb;
            case PixelFormat::eRGBA8_UNorm:
                return WGPUTextureFormat_RGBA8Unorm;
            case PixelFormat::eRGBA8_sRGB:
                return WGPUTextureFormat_RGBA8UnormSrgb;
            case PixelFormat::eDepth32F:
                return WGPUTextureFormat_Depth32Float;
            case PixelFormat::eDepth24_Stencil8:
                return WGPUTextureFormat_Depth24PlusStencil8;
            case PixelFormat::eDepth32F_Stencil8:
                return WGPUTextureFormat_Depth32FloatStencil8;
            case PixelFormat::eStencil8:
                return WGPUTextureFormat_Stencil8;
            default:
                return WGPUTextureFormat_Undefined;
        }
    }

    [[nodiscard]] constexpr WGPUVertexFormat toWgpuVertexFormat(const VertexAttribute::Type type)
    {
        switch (type)
        {
            case VertexAttribute::Type::eFloat:
                return WGPUVertexFormat_Float32;
            case VertexAttribute::Type::eFloat2:
                return WGPUVertexFormat_Float32x2;
            case VertexAttribute::Type::eFloat3:
                return WGPUVertexFormat_Float32x3;
            case VertexAttribute::Type::eFloat4:
                return WGPUVertexFormat_Float32x4;
            case VertexAttribute::Type::eInt4:
                return WGPUVertexFormat_Sint32x4;
            case VertexAttribute::Type::eUByte4_Norm:
                return WGPUVertexFormat_Unorm8x4;
        }
        return WGPUVertexFormat_Float32x4;
    }

    [[nodiscard]] constexpr WGPUShaderStage toWgpuShaderStages(const ShaderStages stages)
    {
        WGPUShaderStage flags = WGPUShaderStage_None;
        if (HasFlagValues(stages, ShaderStages::eVertex))
        {
            flags |= WGPUShaderStage_Vertex;
        }
        if (HasFlagValues(stages, ShaderStages::eFragment))
        {
            flags |= WGPUShaderStage_Fragment;
        }
        if (HasFlagValues(stages, ShaderStages::eCompute))
        {
            flags |= WGPUShaderStage_Compute;
        }
        return flags;
    }

    [[nodiscard]] constexpr WGPUTextureAspect toWgpuTextureAspect(const ImageAspectFlags aspectMask)
    {
        if (HasFlagValues(aspectMask, ImageAspectFlags::eDepth) &&
            HasFlagValues(aspectMask, ImageAspectFlags::eStencil))
        {
            return WGPUTextureAspect_All;
        }
        if (HasFlagValues(aspectMask, ImageAspectFlags::eDepth))
        {
            return WGPUTextureAspect_DepthOnly;
        }
        if (HasFlagValues(aspectMask, ImageAspectFlags::eStencil))
        {
            return WGPUTextureAspect_StencilOnly;
        }
        return WGPUTextureAspect_All;
    }

    [[nodiscard]] constexpr WGPUTextureViewDimension toWgpuTextureViewDimension(const TextureType textureType)
    {
        switch (textureType)
        {
            using enum TextureType;
            case eTexture1D:
                return WGPUTextureViewDimension_2D;
            case eTexture1DArray:
                return WGPUTextureViewDimension_2DArray;
            case eTexture2D:
                return WGPUTextureViewDimension_2D;
            case eTexture2DArray:
                return WGPUTextureViewDimension_2DArray;
            case eTextureCube:
                return WGPUTextureViewDimension_Cube;
            case eTextureCubeArray:
                return WGPUTextureViewDimension_CubeArray;
            case eTexture3D:
                return WGPUTextureViewDimension_3D;
            default:
                return WGPUTextureViewDimension_2D;
        }
    }

    [[nodiscard]] constexpr WGPUFilterMode toWgpuFilter(const TexelFilter filter)
    {
        return filter == TexelFilter::eNearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    }

    [[nodiscard]] constexpr WGPUMipmapFilterMode toWgpuMipmapFilter(const MipmapMode mode)
    {
        return mode == MipmapMode::eNearest ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
    }

    [[nodiscard]] constexpr WGPUAddressMode toWgpuAddressMode(const SamplerAddressMode mode)
    {
        switch (mode)
        {
            case SamplerAddressMode::eRepeat:
                return WGPUAddressMode_Repeat;
            case SamplerAddressMode::eMirroredRepeat:
                return WGPUAddressMode_MirrorRepeat;
            case SamplerAddressMode::eClampToBorder:
            case SamplerAddressMode::eMirrorClampToEdge:
            case SamplerAddressMode::eClampToEdge:
            default:
                return WGPUAddressMode_ClampToEdge;
        }
    }

    [[nodiscard]] constexpr WGPUTextureUsage toWgpuTextureUsage(const ImageUsage usageFlags)
    {
        WGPUTextureUsage usage = WGPUTextureUsage_None;
        if (HasFlagValues(usageFlags, ImageUsage::eSampled))
        {
            usage |= WGPUTextureUsage_TextureBinding;
        }
        if (HasFlagValues(usageFlags, ImageUsage::eStorage))
        {
            usage |= WGPUTextureUsage_StorageBinding;
        }
        if (HasFlagValues(usageFlags, ImageUsage::eRenderTarget))
        {
            usage |= WGPUTextureUsage_RenderAttachment;
        }
        if (HasFlagValues(usageFlags, ImageUsage::eTransferSrc) || HasFlagValues(usageFlags, ImageUsage::eTransfer))
        {
            usage |= WGPUTextureUsage_CopySrc;
        }
        if (HasFlagValues(usageFlags, ImageUsage::eTransferDst) || HasFlagValues(usageFlags, ImageUsage::eTransfer))
        {
            usage |= WGPUTextureUsage_CopyDst;
        }
        return usage == WGPUTextureUsage_None ? (WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding) : usage;
    }

    [[nodiscard]] constexpr PixelFormat toPixelFormat(const WGPUTextureFormat format)
    {
        switch (format)
        {
            case WGPUTextureFormat_BGRA8Unorm:
                return PixelFormat::eBGRA8_UNorm;
            case WGPUTextureFormat_BGRA8UnormSrgb:
                return PixelFormat::eBGRA8_sRGB;
            case WGPUTextureFormat_RGBA8Unorm:
                return PixelFormat::eRGBA8_UNorm;
            case WGPUTextureFormat_RGBA8UnormSrgb:
                return PixelFormat::eRGBA8_sRGB;
            default:
                return PixelFormat::eUndefined;
        }
    }

    [[nodiscard]] constexpr WGPUTextureFormat preferredSwapchainFormat(const SwapchainFormat format)
    {
        return format == SwapchainFormat::esRGB ? WGPUTextureFormat_BGRA8UnormSrgb : WGPUTextureFormat_BGRA8Unorm;
    }

    [[nodiscard]] constexpr bool matchesSwapchainFormat(const WGPUTextureFormat textureFormat,
                                                        const SwapchainFormat   requestedFormat)
    {
        if (requestedFormat == SwapchainFormat::esRGB)
        {
            return textureFormat == WGPUTextureFormat_BGRA8UnormSrgb ||
                   textureFormat == WGPUTextureFormat_RGBA8UnormSrgb;
        }
        return textureFormat == WGPUTextureFormat_BGRA8Unorm || textureFormat == WGPUTextureFormat_RGBA8Unorm;
    }

    [[nodiscard]] constexpr WGPUPresentMode toWgpuPresentMode(const VerticalSync vsync)
    {
        switch (vsync)
        {
            case VerticalSync::eDisabled:
                return WGPUPresentMode_Immediate;
            case VerticalSync::eEnabled:
                return WGPUPresentMode_Fifo;
            case VerticalSync::eAdaptive:
                return WGPUPresentMode_Mailbox;
        }
        return WGPUPresentMode_Fifo;
    }
} // namespace vultra::rhi::webgpu
#endif
