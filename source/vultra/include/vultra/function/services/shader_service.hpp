#pragma once

#include "vultra/core/rhi/shader_library.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IShaderService
    {
    public:
        SERVICE_REGISTER(IShaderService)

        virtual rhi::ShaderLibraryRuntime& builtinLibrary() = 0;
    };
} // namespace vultra
