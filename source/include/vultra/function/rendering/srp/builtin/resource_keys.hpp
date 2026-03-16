#pragma once

#include "vultra/function/framegraph/framegraph_data_registry.hpp"

#include <vbase/core/hash.hpp>

namespace vultra
{
    constexpr FrameGraphResourceKey kResKey_FinalCompositionSource {.id = vbase::hashLiteral("FinalCompositionSource")};
    constexpr FrameGraphResourceKey kResKey_MeshletCullDone {.id = vbase::hashLiteral("MeshletCullDone")};
    constexpr FrameGraphResourceKey kResKey_MeshletBuildDone {.id = vbase::hashLiteral("MeshletBuildDone")};
    constexpr FrameGraphResourceKey kResKey_GaussianSplatCullDone {.id = vbase::hashLiteral("GaussianSplatCullDone")};
    constexpr FrameGraphResourceKey kResKey_GaussianSplatRenderDone {.id =
                                                                         vbase::hashLiteral("GaussianSplatRenderDone")};
} // namespace vultra