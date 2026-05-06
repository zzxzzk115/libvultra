#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        enum class ShaderType
        {
            eVertex,
            eGeometry,
            eFragment,
            eCompute,
            eRayGen,
            eMiss,
            eClosestHit,
            eAnyHit,
            eIntersect,
            eMesh,
            eTask,
        };

        enum class ShaderStages : uint64_t
        {
            eNone       = 0,
            eVertex     = BIT(0),
            eGeometry   = BIT(1),
            eFragment   = BIT(2),
            eCompute    = BIT(3),
            eRayGen     = BIT(4),
            eMiss       = BIT(5),
            eClosestHit = BIT(6),
            eAnyHit     = BIT(7),
            eIntersect  = BIT(8),
            eMesh       = BIT(9),
            eTask       = BIT(10),
        };

        using SPIRV = std::vector<uint32_t>;
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::ShaderStages> : std::true_type
{};
