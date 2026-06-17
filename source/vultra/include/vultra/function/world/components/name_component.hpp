#pragma once

#include "vultra/function/world/components/meta_component.hpp"

namespace vultra
{
    // Deprecated alias: the display name now lives in MetaComponent (MetaComponent::name).
    // Kept so existing call sites keep compiling; prefer MetaComponent directly.
    using NameComponent = MetaComponent;
} // namespace vultra
