#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

namespace vultra
{
    namespace rhi
    {
        // Backend-neutral access mask used by the public RHI layer.
        enum class Access : uint64_t
        {
            eNone = 0,

            eIndexRead                   = BIT(0),
            eVertexAttributeRead         = BIT(1),
            eIndirectCommandRead         = BIT(2),
            eUniformRead                 = BIT(3),
            eShaderRead                  = BIT(4),
            eShaderWrite                 = BIT(5),
            eColorAttachmentRead         = BIT(6),
            eColorAttachmentWrite        = BIT(7),
            eDepthStencilAttachmentRead  = BIT(8),
            eDepthStencilAttachmentWrite = BIT(9),
            eTransferRead                = BIT(10),
            eTransferWrite               = BIT(11),
            eMemoryRead                  = BIT(12),
            eMemoryWrite                 = BIT(13),
            eShaderStorageRead           = BIT(14),
            eShaderStorageWrite          = BIT(15),

            // Raytracing
            eAccelerationStructureRead  = BIT(16),
            eAccelerationStructureWrite = BIT(17),
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::Access> : std::true_type
{};
