#pragma once

#include "vultra/function/world/components/meta_component.hpp"

namespace vultra
{
    // Deprecated alias: active/visible/locked/selectable now live in MetaComponent.
    // Kept so existing call sites keep compiling; prefer MetaComponent directly.
    using EntityStatusComponent = MetaComponent;
} // namespace vultra
