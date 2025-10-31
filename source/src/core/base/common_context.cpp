#include "vultra/core/base/common_context.hpp"

namespace vultra
{
    CommonContext::CommonContext() : logger(Logger::Builder {}.build()), debugDraw(createRef<DebugDrawInterface>()) {}

    void CommonContext::cleanup()
    {
        debugDraw.reset();
        debugDraw = nullptr;
    }

    CommonContext commonContext;
} // namespace vultra