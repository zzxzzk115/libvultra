#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/scoped_enum_flags.hpp"
#include "vultra/core/rhi/cube_face.hpp"
#include "vultra/core/rhi/image_aspect.hpp"
#include "vultra/core/rhi/pipeline_stage.hpp"

#include <cstdint>
#include <optional>

namespace vultra
{
    namespace framegraph
    {
        enum class PipelineStage
        {
            eTransfer         = BIT(0),
            eVertexShader     = BIT(1),
            eGeometryShader   = BIT(2),
            eFragmentShader   = BIT(3),
            eComputeShader    = BIT(4),
            eRayTracingShader = BIT(5),
        };

        enum class ClearValue
        {
            eZero,
            eOne,

            eOpaqueBlack,
            eOpaqueWhite,
            eTransparentBlack,
            eTransparentWhite,

            eFakeSky,

            eUIntMax,
        };

        struct Attachment
        {
            uint32_t                     index {0};
            rhi::ImageAspect             imageAspect;
            std::optional<uint32_t>      layer;
            std::optional<rhi::CubeFace> face;
            std::optional<ClearValue>    clearValue;

            [[nodiscard]] operator uint32_t() const;
        };
        [[nodiscard]] Attachment decodeAttachment(uint32_t bits);

        [[nodiscard]] bool holdsAttachment(uint32_t bits);

        struct Location
        {
            uint32_t set {0};
            uint32_t binding {0};

            [[nodiscard]] operator uint32_t() const;
        };
        [[nodiscard]] Location decodeLocation(uint32_t bits);

        struct BindingInfo
        {
            Location      location;
            PipelineStage pipelineStage;

            [[nodiscard]] operator uint32_t() const;
        };
        [[nodiscard]] BindingInfo decodeBindingInfo(uint32_t bits);

        struct TextureRead
        {
            BindingInfo binding;

            enum class Type
            {
                eCombinedImageSampler,
                eSampledImage,
                eStorageImage
            };
            Type             type;
            rhi::ImageAspect imageAspect;

            [[nodiscard]] operator uint32_t() const;
        };
        [[nodiscard]] TextureRead decodeTextureRead(uint32_t bits);

        struct ImageWrite
        {
            BindingInfo      binding;
            rhi::ImageAspect imageAspect;

            [[nodiscard]] operator uint32_t() const;
        };
        [[nodiscard]] ImageWrite decodeImageWrite(uint32_t bits);

        [[nodiscard]] rhi::PipelineStages convert(const PipelineStage);
    } // namespace framegraph
} // namespace vultra

template<>
struct HasFlags<vultra::framegraph::PipelineStage> : std::true_type
{};
