#include "vultra/core/base/common_context.hpp"

#include "vultra/function/debug_draw/debug_draw_interface.hpp" // complete type for Ref<DebugDrawInterface>

namespace vultra
{
    CommonContext::CommonContext() : logger(Logger::Builder {}.build()) {}

    void CommonContext::cleanup() { debugDraw.reset(); }

    CommonContext commonContext;
} // namespace vultra