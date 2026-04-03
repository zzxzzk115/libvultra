#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"

#include <builtin_shaders.hpp>

namespace vultra
{
    bool ShaderSystem::onInit()
    {
        VULTRA_CORE_INFO("[ShaderSystem] Initializing...");

        const auto backendApi = ctx().config.render.backendApi;
        const bool useWebGpuLibrary = backendApi == rhi::RenderBackendApi::eWebGPU;

        const uint8_t* shaderLibData = nullptr;
        size_t         shaderLibSize = 0;

        if (useWebGpuLibrary)
        {
            shaderLibData = builtin_shaders_web_vshweblib;
            shaderLibSize = builtin_shaders_web_vshweblib_size;
        }
#if defined(__ANDROID__)
        else
        {
            shaderLibData = builtin_shaders_android_vshlib;
            shaderLibSize = builtin_shaders_android_vshlib_size;
        }
#else
        else
        {
            shaderLibData = builtin_shaders_desktop_vshlib;
            shaderLibSize = builtin_shaders_desktop_vshlib_size;
        }
#endif

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
