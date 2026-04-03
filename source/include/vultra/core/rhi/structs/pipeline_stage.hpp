#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

namespace vultra
{
    namespace rhi
    {
        // Backend-neutral pipeline stage mask used by the public RHI layer.
        enum class PipelineStages : uint64_t
        {
            eNone = 0,

            eTop                        = BIT(0),
            eDrawIndirect               = BIT(1),
            eVertexInput                = BIT(2),
            eVertexShader               = BIT(3),
            eGeometryShader             = BIT(4),
            eFragmentShader             = BIT(5),
            eEarlyFragmentTest          = BIT(6),
            eLateFragmentTest           = BIT(7),
            eFragmentTests              = eEarlyFragmentTest | eLateFragmentTest,
            eColorAttachmentOutput      = BIT(8),
            eComputeShader              = BIT(9),
            eRayTracingShader           = BIT(10),
            eAccelerationStructureBuild = BIT(11),
            eTransfer                   = BIT(12),
            eBlit                       = BIT(13),
            eBottom                     = BIT(14),

            eAllTransfer = BIT(15),
            eAllGraphics = BIT(16),
            eAllCommands = BIT(17),
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::PipelineStages> : std::true_type
{};
