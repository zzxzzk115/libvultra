#include "vultra/core/rhi/command_buffer.hpp"

namespace vultra::rhi
{
    namespace
    {
        BuiltinProfilerGpuScopeCallbacks g_BuiltinProfilerGpuScopeCallbacks {};
    }

    BuiltinProfilerGpuScopeCallbacks& builtinProfilerGpuScopeCallbacks()
    {
        return g_BuiltinProfilerGpuScopeCallbacks;
    }

    void setBuiltinProfilerGpuScopeCallbacks(
        std::function<void(const BuiltinProfilerGpuScopeContext&)> bind,
        std::function<void(const BuiltinProfilerGpuScopeContext&, const char*)> begin,
        std::function<void(const BuiltinProfilerGpuScopeContext&)>               end)
    {
        auto& callbacks = builtinProfilerGpuScopeCallbacks();
        callbacks.bind  = std::move(bind);
        callbacks.begin = std::move(begin);
        callbacks.end   = std::move(end);
    }
} // namespace vultra::rhi
