#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"

#include <builtin_shaders.hpp>

namespace vultra
{
    bool ShaderSystem::onInit()
    {
        VULTRA_CORE_INFO("[ShaderSystem] Initializing...");

        const auto backendApi           = ctx().config.render.backendApi;
        const auto builtinShaderLibrary = ctx().config.render.builtinShaderLibrary;
        const bool useWebGpuLibrary     = backendApi == rhi::RenderBackendApi::eWebGPU;
        const bool useCompatibilityLibrary =
            builtinShaderLibrary == EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eCompatibility;

        const uint8_t* shaderLibData = nullptr;
        size_t         shaderLibSize = 0;

        if (useWebGpuLibrary)
        {
            shaderLibData = builtin_shaders_compatibility_web_vshweblib;
            shaderLibSize = builtin_shaders_compatibility_web_vshweblib_size;
        }
        else
        {
#if defined(__ANDROID__)
            shaderLibData = builtin_shaders_compatibility_vshlib;
            shaderLibSize = builtin_shaders_compatibility_vshlib_size;
#else
            if (useCompatibilityLibrary)
            {
                shaderLibData = builtin_shaders_compatibility_vshlib;
                shaderLibSize = builtin_shaders_compatibility_vshlib_size;
            }
            else
            {
                shaderLibData = builtin_shaders_highend_vshlib;
                shaderLibSize = builtin_shaders_highend_vshlib_size;
            }
#endif
        }

        // Load builtin shader library from embedded header.
        if (!m_BuiltinShaderLibrary.loadFromMemory(shaderLibData, shaderLibSize))
        {
            VULTRA_CORE_ERROR("[ShaderSystem] Failed to load builtin shader library");
            return false;
        }

        VULTRA_CORE_TRACE("[ShaderSystem] Providing IShaderService");
        ctx().services.provide<IShaderService>(this);

        return true;
    }

    void ShaderSystem::onShutdown() { VULTRA_CORE_INFO("[ShaderSystem] Shutting down"); }
} // namespace vultra
