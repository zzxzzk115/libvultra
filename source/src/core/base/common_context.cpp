#include "vultra/core/base/common_context.hpp"

namespace vultra
{
    CommonContext::CommonContext() : logger(Logger::Builder {}.build()) {}

    CommonContext commonContext;
} // namespace vultra