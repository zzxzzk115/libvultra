#include "vultra/core/base/common_context.hpp"

namespace vultra
{
    CommonContext::CommonContext() : logger(Logger::Builder {}.build()) {}

    void CommonContext::cleanup() {}

    CommonContext commonContext;
} // namespace vultra