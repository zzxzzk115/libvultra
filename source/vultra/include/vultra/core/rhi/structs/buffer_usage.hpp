#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        enum class BufferUsage
        {
            eNone                   = ZERO_BIT,
            eTransferSrc            = BIT(0),
            eTransferDst            = BIT(1),
            eTransfer               = eTransferSrc | eTransferDst,
            eVertexBuffer           = BIT(2),
            eIndexBuffer            = BIT(3),
            eUniformBuffer          = BIT(4),
            eStorageBuffer          = BIT(5),
            eIndirectBuffer         = BIT(6),
            eShaderDeviceAddress    = BIT(7),
            eAccelerationBuildInput = BIT(8),
            eAccelerationStorage    = BIT(9),
            eShaderBindingTable     = BIT(10),
        };

        [[nodiscard]] std::string_view toString(BufferUsage);
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::BufferUsage> : std::true_type
{};
