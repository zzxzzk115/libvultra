#pragma once

#include "vultra/function/rendering/shader/shader_library.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IShaderService
    {
    public:
        SERVICE_REGISTER(IShaderService)

        virtual rendering::ShaderLibraryRuntime& bulitinLibrary() = 0;
    };
} // namespace vultra
