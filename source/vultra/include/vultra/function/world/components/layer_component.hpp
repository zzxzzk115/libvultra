#pragma once

#include "vultra/function/world/components/meta_component.hpp"

namespace vultra
{
    // Deprecated alias: the render layer now lives in MetaComponent (MetaComponent::layer).
    // The kRenderLayer* constants live in meta_component.hpp. Kept so existing call sites keep compiling.
    using LayerComponent = MetaComponent;
} // namespace vultra
