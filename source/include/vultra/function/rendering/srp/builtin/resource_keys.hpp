#pragma once

#include "vultra/function/framegraph/framegraph_data_registry.hpp"

#include <vbase/core/hash.hpp>

namespace vultra
{
    constexpr FrameGraphResourceKey kResKey_FinalCompositionTarget {.id = vbase::hashLiteral("FinalCompositionTarget")};
}