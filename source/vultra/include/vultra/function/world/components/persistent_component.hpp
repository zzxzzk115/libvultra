#pragma once

#include "vultra/function/world/components/meta_component.hpp"

namespace vultra
{
    // Deprecated alias: the DontDestroyOnLoad marker now lives in MetaComponent::keepOnLoad.
    // NOTE: because MetaComponent is on every entity, presence no longer implies persistence --
    // test MetaComponent::keepOnLoad, not all_of<PersistentComponent>(). Set it via
    // Scene.dontDestroyOnLoad(entity). Kept so existing call sites keep compiling.
    using PersistentComponent = MetaComponent;
} // namespace vultra
