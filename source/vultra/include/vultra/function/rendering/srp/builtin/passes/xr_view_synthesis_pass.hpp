#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <span>
#include <string>
#include <string_view>

namespace vultra
{
    struct XrViewSynthesisSettings
    {
        bool        enabled {true};
        std::string warpingBackend {"adaptive_mesh_graphics"};
        std::string inpaintingBackend {"pull_push"};
        std::string sourceView {"left"};
        std::string targetView {"right"};

        uint32_t baseGridSize {16};
        uint32_t maxSubdivision {3};
        float    sideLengthThreshold {0.1f};
        float    depthThreshold {0.015f};
    };

    struct XrAdaptiveMeshData
    {
        FrameGraphResource vertices;
        uint32_t           vertexCount {0};
    };

    struct XrPullPushMipData
    {
        FrameGraphResource pyramid;
        FrameGraphResource output;
    };

    class XrAdaptiveMeshBuildPass final : public rhi::ComputePass<XrAdaptiveMeshBuildPass>
    {
        friend class BasePass;

    public:
        XrAdaptiveMeshBuildPass();

        [[nodiscard]] XrAdaptiveMeshData addPass(FrameGraphBuildContext&            ctx,
                                                 FrameGraphResource                 source,
                                                 FrameGraphResource                 depth,
                                                 const XrViewSynthesisSettings& settings);

    private:
        [[nodiscard]] rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };

    class XrAdaptiveMeshRasterPass final : public rhi::RenderPass<XrAdaptiveMeshRasterPass>
    {
        friend class BasePass;

    public:
        XrAdaptiveMeshRasterPass();

        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&            ctx,
                                                 const XrAdaptiveMeshData&          mesh,
                                                 FrameGraphResource                 source,
                                                 const XrViewSynthesisSettings& settings);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPullPyramidPass final : public rhi::RenderPass<XrPullPyramidPass>
    {
        friend class BasePass;

    public:
        XrPullPyramidPass();

        [[nodiscard]] XrPullPushMipData addPass(FrameGraphBuildContext& ctx,
                                                FrameGraphResource      pyramid,
                                                uint32_t                lod,
                                                rhi::Extent2D           dstExtent);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPushPyramidPass final : public rhi::RenderPass<XrPushPyramidPass>
    {
        friend class BasePass;

    public:
        XrPushPyramidPass();

        [[nodiscard]] XrPullPushMipData addPass(FrameGraphBuildContext& ctx,
                                                FrameGraphResource      pyramid,
                                                uint32_t                lod,
                                                rhi::Extent2D           dstExtent);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPullPushInpaintPass final
    {
    public:
        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&            ctx,
                                                 FrameGraphResource                 warped,
                                                 const XrViewSynthesisSettings& settings);

    private:
        XrPullPyramidPass m_PullPass;
        XrPushPyramidPass m_PushPass;
    };

    class XrViewSynthesisPass final
    {
    public:
        struct BackendInfo
        {
            std::string_view value;
            std::string_view label;
            std::string_view description;
        };

        struct ViewInfo
        {
            std::string_view value;
            std::string_view label;
        };

        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&            ctx,
                                                 FrameGraphResource                 source,
                                                 FrameGraphResource                 depth,
                                                 const XrViewSynthesisSettings& settings);

        [[nodiscard]] static std::span<const BackendInfo> warpingBackends();
        [[nodiscard]] static std::span<const BackendInfo> inpaintingBackends();
        [[nodiscard]] static std::span<const ViewInfo>    sourceViews();
        [[nodiscard]] static std::span<const ViewInfo>    targetViews();
        [[nodiscard]] static bool                         hasWarpingBackend(std::string_view name);
        [[nodiscard]] static bool                         hasInpaintingBackend(std::string_view name);

    private:
        XrAdaptiveMeshBuildPass  m_AdaptiveMeshBuildPass;
        XrAdaptiveMeshRasterPass m_AdaptiveMeshRasterPass;
        XrPullPushInpaintPass    m_PullPushInpaintPass;
    };
} // namespace vultra
