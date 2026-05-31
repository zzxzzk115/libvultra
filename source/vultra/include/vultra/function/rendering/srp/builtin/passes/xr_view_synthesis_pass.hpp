#pragma once

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
        std::string warpingBackend {"geometry"};
        std::string inpaintingBackend {"pull_push"};
        std::string sourceView {"left"};
        std::string targetView {"right"};

        uint32_t gridSize {4};
        float    warpStrength {0.035f};
    };

    struct XrPullPushMipData
    {
        FrameGraphResource pyramid;
        FrameGraphResource output;
    };

    class IXrWarpingBackend
    {
    public:
        virtual ~IXrWarpingBackend() = default;

        [[nodiscard]] virtual std::string_view   name() const                                     = 0;
        [[nodiscard]] virtual FrameGraphResource addPass(FrameGraphBuildContext&        ctx,
                                                         FrameGraphResource             source,
                                                         FrameGraphResource             depth,
                                                         const XrViewSynthesisSettings& settings) = 0;
    };

    class IXrInpaintingBackend
    {
    public:
        virtual ~IXrInpaintingBackend() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual FrameGraphResource
        addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, const XrViewSynthesisSettings& settings) = 0;
    };

    class XrGeometryWarpPass final : public rhi::RenderPass<XrGeometryWarpPass>, public IXrWarpingBackend
    {
        friend class BasePass;

    public:
        XrGeometryWarpPass();

        [[nodiscard]] std::string_view   name() const override;
        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&        ctx,
                                                 FrameGraphResource             source,
                                                 FrameGraphResource             depth,
                                                 const XrViewSynthesisSettings& settings) override;

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPullPyramidPass final : public rhi::RenderPass<XrPullPyramidPass>
    {
        friend class BasePass;

    public:
        XrPullPyramidPass();

        [[nodiscard]] XrPullPushMipData
        addPass(FrameGraphBuildContext& ctx, FrameGraphResource pyramid, uint32_t lod, rhi::Extent2D dstExtent);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPushPyramidPass final : public rhi::RenderPass<XrPushPyramidPass>
    {
        friend class BasePass;

    public:
        XrPushPyramidPass();

        [[nodiscard]] XrPullPushMipData
        addPass(FrameGraphBuildContext& ctx, FrameGraphResource pyramid, uint32_t lod, rhi::Extent2D dstExtent);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };

    class XrPullPushInpaintPass final : public IXrInpaintingBackend
    {
    public:
        [[nodiscard]] std::string_view   name() const override;
        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&        ctx,
                                                 FrameGraphResource             warped,
                                                 const XrViewSynthesisSettings& settings) override;

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

        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&        ctx,
                                                 FrameGraphResource             source,
                                                 FrameGraphResource             depth,
                                                 const XrViewSynthesisSettings& settings);

        [[nodiscard]] static std::span<const BackendInfo> warpingBackends();
        [[nodiscard]] static std::span<const BackendInfo> inpaintingBackends();
        [[nodiscard]] static std::span<const ViewInfo>    sourceViews();
        [[nodiscard]] static std::span<const ViewInfo>    targetViews();
        [[nodiscard]] static bool                         hasWarpingBackend(std::string_view name);
        [[nodiscard]] static bool                         hasInpaintingBackend(std::string_view name);

    private:
        XrGeometryWarpPass    m_GeometryWarpPass;
        XrPullPushInpaintPass m_PullPushInpaintPass;
    };
} // namespace vultra
