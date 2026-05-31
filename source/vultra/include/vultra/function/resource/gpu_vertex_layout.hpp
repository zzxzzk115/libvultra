#pragma once

#include "vultra/core/rhi/shader_library.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"

#include <cstdint>

namespace vultra::resource
{
    inline constexpr uint32_t kVertexLocationPosition  = 0u;
    inline constexpr uint32_t kVertexLocationNormal    = 1u;
    inline constexpr uint32_t kVertexLocationColor     = 2u;
    inline constexpr uint32_t kVertexLocationTexCoord0 = 3u;
    inline constexpr uint32_t kVertexLocationTexCoord1 = 4u;
    inline constexpr uint32_t kVertexLocationTangent   = 5u;
    inline constexpr uint32_t kVertexLocationJointIndices = 6u;
    inline constexpr uint32_t kVertexLocationJointWeights = 7u;
    inline constexpr uint32_t kInvalidVertexAttributeOffset = 0xFFFFFFFFu;

    enum class GpuVertexAttributeFlags : uint32_t
    {
        eNone      = 0u,
        ePosition  = 1u << 0u,
        eNormal    = 1u << 1u,
        eColor     = 1u << 2u,
        eTexCoord0 = 1u << 3u,
        eTexCoord1 = 1u << 4u,
        eTangent   = 1u << 5u,
        eJointIndices = 1u << 6u,
        eJointWeights = 1u << 7u,
    };

    [[nodiscard]] constexpr uint32_t gpuVertexAttributeFlagMask(const GpuVertexAttributeFlags flag)
    {
        return static_cast<uint32_t>(flag);
    }

    [[nodiscard]] constexpr bool gpuVertexAttributeHasFlag(const uint32_t flags, const GpuVertexAttributeFlags flag)
    {
        return (flags & gpuVertexAttributeFlagMask(flag)) != 0u;
    }

    struct GpuVertexLayout
    {
        uint32_t attributeMask {0u};
        uint32_t positionOffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t normalOffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t colorOffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t texCoord0OffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t texCoord1OffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t tangentOffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t jointIndicesOffsetBytes {kInvalidVertexAttributeOffset};
        uint32_t jointWeightsOffsetBytes {kInvalidVertexAttributeOffset};

        [[nodiscard]] bool hasPosition() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::ePosition);
        }

        [[nodiscard]] bool hasNormal() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eNormal);
        }

        [[nodiscard]] bool hasColor() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eColor);
        }

        [[nodiscard]] bool hasTexCoord0() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eTexCoord0);
        }

        [[nodiscard]] bool hasTexCoord1() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eTexCoord1);
        }

        [[nodiscard]] bool hasTangent() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eTangent);
        }

        [[nodiscard]] bool hasSkinning() const
        {
            return gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eJointIndices) &&
                   gpuVertexAttributeHasFlag(attributeMask, GpuVertexAttributeFlags::eJointWeights);
        }
    };

    [[nodiscard]] inline GpuVertexLayout inspectGpuVertexLayout(const rhi::VertexAttributes& attributes)
    {
        GpuVertexLayout out {};

        const auto read = [&attributes](const uint32_t location,
                                        uint32_t&      offset,
                                        const GpuVertexAttributeFlags flag,
                                        uint32_t& mask) {
            if (const auto it = attributes.find(location); it != attributes.end())
            {
                offset = it->second.offset;
                mask |= gpuVertexAttributeFlagMask(flag);
            }
        };

        read(kVertexLocationPosition, out.positionOffsetBytes, GpuVertexAttributeFlags::ePosition, out.attributeMask);
        read(kVertexLocationNormal, out.normalOffsetBytes, GpuVertexAttributeFlags::eNormal, out.attributeMask);
        read(kVertexLocationColor, out.colorOffsetBytes, GpuVertexAttributeFlags::eColor, out.attributeMask);
        read(kVertexLocationTexCoord0, out.texCoord0OffsetBytes, GpuVertexAttributeFlags::eTexCoord0, out.attributeMask);
        read(kVertexLocationTexCoord1, out.texCoord1OffsetBytes, GpuVertexAttributeFlags::eTexCoord1, out.attributeMask);
        read(kVertexLocationTangent, out.tangentOffsetBytes, GpuVertexAttributeFlags::eTangent, out.attributeMask);
        read(kVertexLocationJointIndices,
             out.jointIndicesOffsetBytes,
             GpuVertexAttributeFlags::eJointIndices,
             out.attributeMask);
        read(kVertexLocationJointWeights,
             out.jointWeightsOffsetBytes,
             GpuVertexAttributeFlags::eJointWeights,
             out.attributeMask);
        return out;
    }

    [[nodiscard]] inline rhi::ShaderLibraryRuntime::KeywordValues shaderKeywordsForVertexLayout(
        const GpuVertexLayout& layout)
    {
        return {
            {"VTX_HAS_NORMAL", layout.hasNormal() ? 1u : 0u},
            {"VTX_HAS_COLOR", layout.hasColor() ? 1u : 0u},
            {"VTX_HAS_UV0", layout.hasTexCoord0() ? 1u : 0u},
            {"VTX_HAS_UV1", layout.hasTexCoord1() ? 1u : 0u},
            {"VTX_HAS_TANGENT", layout.hasTangent() ? 1u : 0u},
            {"VTX_HAS_SKIN", layout.hasSkinning() ? 1u : 0u},
        };
    }

    [[nodiscard]] inline rhi::VertexAttributes buildInputAssemblyVertexAttributes(
        const GpuVertexLayout& layout,
        const bool             requireNormal,
        const bool             includeTexCoord0,
        const bool             includeTangent)
    {
        rhi::VertexAttributes attrs;
        if (layout.hasPosition())
        {
            attrs[kVertexLocationPosition] = rhi::VertexAttribute {
                .location = kVertexLocationPosition,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = layout.positionOffsetBytes,
            };
        }
        if (requireNormal && layout.hasNormal())
        {
            attrs[kVertexLocationNormal] = rhi::VertexAttribute {
                .location = kVertexLocationNormal,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = layout.normalOffsetBytes,
            };
        }
        if (includeTexCoord0 && layout.hasTexCoord0())
        {
            attrs[kVertexLocationTexCoord0] = rhi::VertexAttribute {
                .location = kVertexLocationTexCoord0,
                .type     = rhi::VertexAttribute::Type::eFloat2,
                .offset   = layout.texCoord0OffsetBytes,
            };
        }
        if (includeTangent && layout.hasTangent())
        {
            attrs[kVertexLocationTangent] = rhi::VertexAttribute {
                .location = kVertexLocationTangent,
                .type     = rhi::VertexAttribute::Type::eFloat4,
                .offset   = layout.tangentOffsetBytes,
            };
        }
        if (layout.hasSkinning())
        {
            attrs[kVertexLocationJointIndices] = rhi::VertexAttribute {
                .location = kVertexLocationJointIndices,
                .type     = rhi::VertexAttribute::Type::eInt4,
                .offset   = layout.jointIndicesOffsetBytes,
            };
            attrs[kVertexLocationJointWeights] = rhi::VertexAttribute {
                .location = kVertexLocationJointWeights,
                .type     = rhi::VertexAttribute::Type::eFloat4,
                .offset   = layout.jointWeightsOffsetBytes,
            };
        }
        return attrs;
    }
} // namespace vultra::resource
