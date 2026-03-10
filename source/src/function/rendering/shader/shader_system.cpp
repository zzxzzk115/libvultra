#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/core/base/common_context.hpp"

#include <builtin_shaders.hpp>

namespace vultra
{
    bool ShaderSystem::onInit()
    {
        VULTRA_CORE_INFO("[ShaderSystem] Initializing...");

        // Load builtin shader library from embedded header.
        if (!m_BuiltinShaderLibrary.loadFromMemory(builtin_shaders_vshlib, builtin_shaders_vshlib_size))
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