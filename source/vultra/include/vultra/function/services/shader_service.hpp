#pragma once

#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/shader_library.hpp"

#include <vbase/service/service_registry.hpp>

#include <string_view>

namespace vultra
{
    class IShaderService
    {
    public:
        SERVICE_REGISTER(IShaderService)

        virtual rhi::ShaderLibraryRuntime& builtinLibrary() = 0;
        virtual rhi::ShaderLibraryRuntime& builtinLibrary(rhi::ShaderProfile profile) = 0;
        virtual rhi::ShaderLibraryRuntime* loadProjectLibrary(std::string_view uri) = 0;
        virtual rhi::ShaderLibraryRuntime* reloadProjectLibrary(std::string_view uri) = 0;
        virtual rhi::ShaderLibraryRuntime* findProjectLibrary(std::string_view uri) = 0;
    };
} // namespace vultra
